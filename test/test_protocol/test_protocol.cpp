// ================================================================
// 长度头协议单元测试
// 验证：4字节长度头 + 消息体 的编解码正确性
// 场景：正常收发、粘包、分包、大消息、边界条件
// ================================================================

#include <cstdint>
#include <cstring>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

// 协议常量（与 include/public.hpp 保持一致）
const int kHeaderLen = 4;

// ─────────────────────────────────────────────────────────────
// 模拟发送端：编码一条消息 → 带长度头的缓冲区
// ─────────────────────────────────────────────────────────────
static std::string encode(const std::string &msg)
{
    uint32_t be_len = __builtin_bswap32(static_cast<uint32_t>(msg.size()));
    std::string buf;
    buf.append(reinterpret_cast<const char *>(&be_len), sizeof(be_len));
    buf.append(msg);
    return buf;
}

// ─────────────────────────────────────────────────────────────
// 模拟接收端：从缓冲区解码一条消息
// 返回值：
//   true  → 成功解码一条消息，out_msg 为消息体，consumed 为消耗的字节数
//   false → 缓冲区数据不足一条完整消息
// ─────────────────────────────────────────────────────────────
static bool decode(const std::string &buffer, std::string &out_msg, size_t &consumed)
{
    if (buffer.size() < static_cast<size_t>(kHeaderLen))
    {
        return false;  // 连长度头都没收全
    }

    uint32_t be_len;
    std::memcpy(&be_len, buffer.data(), sizeof(be_len));
    uint32_t len = __builtin_bswap32(be_len);

    if (buffer.size() < static_cast<size_t>(kHeaderLen + len))
    {
        return false;  // 长度头收全了，但消息体还没到
    }

    out_msg = buffer.substr(kHeaderLen, len);
    consumed = kHeaderLen + len;
    return true;
}

// ═════════════════════════════════════════════════════════════
// 测试用例
// ═════════════════════════════════════════════════════════════

static int test_count = 0;
static int pass_count = 0;

#define TEST(name)                                              \
    do                                                          \
    {                                                           \
        test_count++;                                           \
        std::cout << "[" << test_count << "] " << name << "... "; \
    } while (0)

#define PASS()                          \
    do                                  \
    {                                   \
        pass_count++;                   \
        std::cout << "PASS" << std::endl; \
    } while (0)

// ── 1. 正常单条消息 ─────────────────────────────────────────
void test_single_message()
{
    TEST("单条消息编解码");

    std::string original = R"({"msgid":1,"id":123,"password":"hello"})";
    std::string buffer = encode(original);

    std::string decoded;
    size_t consumed = 0;
    bool ok = decode(buffer, decoded, consumed);

    assert(ok);
    assert(decoded == original);
    assert(consumed == buffer.size());
    PASS();
}

// ── 2. 粘包：两条消息拼在一起 ───────────────────────────────
void test_sticky_packet()
{
    TEST("粘包场景：两条完整消息合并到达");

    std::string msg1 = R"({"msgid":1,"id":1})";
    std::string msg2 = R"({"msgid":6,"toid":2,"msg":"hi"})";

    // 模拟 TCP 粘包：将两条消息的编码结果直接拼接
    std::string buffer = encode(msg1) + encode(msg2);

    // 解码第一条
    std::string out1;
    size_t consumed = 0;
    assert(decode(buffer, out1, consumed));
    assert(out1 == msg1);

    // 在剩余部分解码第二条
    std::string remaining = buffer.substr(consumed);
    std::string out2;
    assert(decode(remaining, out2, consumed));
    assert(out2 == msg2);

    // 验证缓冲区全部消耗完毕
    assert(remaining.size() == consumed);
    PASS();
}

// ── 3. 分包：长度头收全但消息体不全 ─────────────────────────
void test_partial_body()
{
    TEST("分包场景：收全了长度头，消息体不完整");

    std::string msg = R"({"msgid":1})";
    std::string buffer = encode(msg);

    // 模拟只收到部分数据：只给长度头 + 1 字节消息体
    std::string partial = buffer.substr(0, kHeaderLen + 1);

    std::string out;
    size_t consumed = 0;
    bool ok = decode(partial, out, consumed);

    assert(!ok);  // 应该解码失败（数据不足）
    assert(consumed == 0);
    assert(out.empty());
    PASS();
}

// ── 4. 分包：连长度头都没收全 ───────────────────────────────
void test_partial_header()
{
    TEST("分包场景：连长度头都没收全");

    std::string buffer(2, '\0');  // 只有 2 字节，不够 4 字节头
    std::string out;
    size_t consumed = 0;

    bool ok = decode(buffer, out, consumed);
    assert(!ok);
    assert(consumed == 0);
    assert(out.empty());
    PASS();
}

// ── 5. 大消息（超过典型 1024 缓冲区）────────────────────────
void test_large_message()
{
    TEST("大消息场景：消息体超过 4KB");

    // 构建一个 5000 字节的消息体
    std::string body(5000, 'A');
    std::string msg = R"({"msgid":6,"data":")" + body + R"("})";

    std::string buffer = encode(msg);
    std::string out;
    size_t consumed = 0;

    assert(decode(buffer, out, consumed));
    assert(out == msg);
    assert(consumed == buffer.size());
    PASS();
}

// ── 6. 混合场景：2条完整 + 1条不完整 ────────────────────────
void test_mixed_sticky_partial()
{
    TEST("混合场景：2条完整消息 + 1条不完整消息");

    std::string msg1 = R"({"msgid":1})";
    std::string msg2 = R"({"msgid":6,"msg":"hello"})";
    std::string msg3 = R"({"msgid":99})";

    // 前两条完整编码，第三条只编码一半（模拟还在传输中）
    std::string full = encode(msg1) + encode(msg2);
    std::string partial = encode(msg3).substr(0, kHeaderLen + 2);  // 头+2字节
    std::string buffer = full + partial;

    // 解码第一条
    std::string out;
    size_t consumed = 0;
    size_t offset = 0;

    assert(decode(buffer.substr(offset), out, consumed));
    assert(out == msg1);
    offset += consumed;

    // 解码第二条
    assert(decode(buffer.substr(offset), out, consumed));
    assert(out == msg2);
    offset += consumed;

    // 第三条数据不足，应该解码失败
    bool ok = decode(buffer.substr(offset), out, consumed);
    assert(!ok);
    PASS();
}

// ── 7. 零长度消息 ──────────────────────────────────────────
void test_empty_message()
{
    TEST("边界条件：空消息体");

    std::string msg;
    std::string buffer = encode(msg);

    std::string out;
    size_t consumed = 0;
    bool ok = decode(buffer, out, consumed);

    assert(ok);
    assert(out.empty());
    assert(consumed == kHeaderLen);
    PASS();
}

// ── 8. 字节序验证 ─────────────────────────────────────────
void test_byte_order()
{
    TEST("字节序验证：长度头为大端（网络字节序）");

    // 一条长度为 6 的消息 "123456"
    std::string msg = "123456";
    std::string buffer = encode(msg);

    // 前 4 字节应该是大端表示的 6
    uint32_t be_len;
    std::memcpy(&be_len, buffer.data(), sizeof(be_len));

    uint32_t expected = __builtin_bswap32(static_cast<uint32_t>(msg.size()));
    assert(be_len == expected);
    PASS();
}

// ── 9. 大量消息循环（压力冒烟） ────────────────────────────
void test_burst()
{
    TEST("冒烟测试：1000 条消息连续收发");

    std::string all;
    std::vector<std::string> messages;

    // 生成 1000 条不同消息并编码
    for (int i = 0; i < 1000; i++)
    {
        std::string m = "{\"seq\":" + std::to_string(i) + ",\"data\":\"hello\"}";
        messages.push_back(m);
        all += encode(m);
    }

    // 全部解码
    size_t offset = 0;
    for (int i = 0; i < 1000; i++)
    {
        std::string out;
        size_t consumed = 0;
        bool ok = decode(all.substr(offset), out, consumed);
        assert(ok);
        assert(out == messages[i]);
        offset += consumed;
    }

    // 验证全部消耗完
    assert(offset == all.size());
    PASS();
}

// ═════════════════════════════════════════════════════════════
// 主函数
// ═════════════════════════════════════════════════════════════
int main()
{
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << "  长度头协议单元测试" << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    test_single_message();
    test_sticky_packet();
    test_partial_body();
    test_partial_header();
    test_large_message();
    test_mixed_sticky_partial();
    test_empty_message();
    test_byte_order();
    test_burst();

    std::cout << "───────────────────────────────────────────" << std::endl;
    std::cout << "  结果: " << pass_count << "/" << test_count << " 通过";
    if (pass_count == test_count)
        std::cout << " ✅";
    else
        std::cout << " ❌";
    std::cout << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    return (pass_count == test_count) ? 0 : 1;
}
