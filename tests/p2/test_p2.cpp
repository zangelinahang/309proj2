#include <string>
#include <string_view>

// The spec explicitly asks the test suite to assert the private pending_
// bound. Include this one class with private exposed only inside this test TU.
#define private public
#include "core/sentinel_scanner.h"
#undef private

#include "core/conversation.h"
#include "core/message.h"
#include "harness/harness.h"
#include "model/replay_client.h"
#include "model/scripted_client.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr const char* kSentinel = "<|end_conversation|>";

std::filesystem::path temp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("ece309_p2_" + name);
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

const char* role_name(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
    }
    return "assistant";
}

void save_mock_transcript(const Conversation& conv,
                          const std::filesystem::path& path) {
    std::ofstream out(path);
    assert(out.is_open());

    bool first = true;
    for (const Message* m = conv.begin(); m != conv.end(); ++m) {
        if (!first) out << "---\n";
        first = false;
        out << "role: " << role_name(m->role()) << "\n";
        out << m->content() << "\n";
    }
}

class VectorInput : public InputSource {
public:
    explicit VectorInput(std::vector<std::string> lines)
        : lines_(std::move(lines)) {}

    std::string read_line() override {
        if (next_ >= lines_.size()) {
            eof_ = true;
            return "";
        }
        eof_ = false;
        return lines_[next_++];
    }

    bool is_eof() const override { return eof_; }

private:
    std::vector<std::string> lines_;
    std::size_t next_ = 0;
    bool eof_ = false;
};

class StringOutput : public OutputSink {
public:
    void write(std::string_view text) override { text_ += text; }
    const std::string& str() const { return text_; }

private:
    std::string text_;
};

void test_message_default_and_accessors() {
    Message empty;
    assert(empty.role() == Role::System);
    assert(empty.content().empty());

    Message user(Role::User, "hello");
    assert(user.role() == Role::User);
    assert(user.content() == "hello");
}

void test_empty_conversation_bounds() {
    Conversation conv;
    assert(conv.size() == 0);
    assert(conv.begin() == conv.end());

    bool threw = false;
    try {
        (void)conv.at(0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

void test_system_message_stays_first() {
    Conversation conv;
    conv.append(Message(Role::System, "system rules"));
    for (int i = 0; i < 100; ++i) {
        conv.append(Message(Role::User, "u" + std::to_string(i)));
    }

    assert(conv.size() == 101);
    assert(conv.at(0).role() == Role::System);
    assert(conv.at(0).content() == "system rules");
}

void test_growth_doubles_without_unnecessary_reallocation() {
    Conversation conv;

    conv.append(Message(Role::User, "0"));
    const Message* p1 = conv.begin();

    conv.append(Message(Role::User, "1"));
    const Message* p2 = conv.begin();
    assert(p2 != p1);                 // 1 -> 2

    conv.append(Message(Role::User, "2"));
    const Message* p3 = conv.begin();
    assert(p3 != p2);                 // 2 -> 4

    conv.append(Message(Role::User, "3"));
    assert(conv.begin() == p3);       // still capacity 4

    conv.append(Message(Role::User, "4"));
    assert(conv.begin() != p3);       // 4 -> 8

    for (std::size_t i = 0; i < conv.size(); ++i) {
        assert(conv.at(i).content() == std::to_string(i));
    }
}

void test_copy_constructor_is_deep() {
    Conversation original;
    original.append(Message(Role::User, "one"));
    original.append(Message(Role::Assistant, "two"));

    Conversation copy(original);
    assert(copy.size() == original.size());
    assert(copy.begin() != original.begin());
    assert(copy.at(0).content() == "one");
    assert(copy.at(1).content() == "two");

    original.append(Message(Role::User, "three"));
    assert(copy.size() == 2);
}

void test_copy_assignment_is_deep_and_self_safe() {
    Conversation source;
    source.append(Message(Role::User, "source"));

    Conversation target;
    target.append(Message(Role::Assistant, "old"));
    target = source;

    assert(target.size() == 1);
    assert(target.begin() != source.begin());
    assert(target.at(0).content() == "source");

    const Message* before = target.begin();
    target = target;
    assert(target.begin() == before);
    assert(target.at(0).content() == "source");
}

void test_move_constructor_steals_buffer() {
    Conversation source;
    source.append(Message(Role::User, "move me"));
    const Message* old_buffer = source.begin();

    Conversation moved(std::move(source));
    assert(moved.begin() == old_buffer);
    assert(moved.size() == 1);
    assert(moved.at(0).content() == "move me");
    assert(source.size() == 0);
    assert(source.begin() == nullptr);
    assert(source.end() == nullptr);
}

void test_move_assignment_steals_buffer() {
    Conversation source;
    source.append(Message(Role::Assistant, "new buffer"));
    const Message* old_buffer = source.begin();

    Conversation target;
    target.append(Message(Role::User, "discard me"));
    target = std::move(source);

    assert(target.begin() == old_buffer);
    assert(target.size() == 1);
    assert(target.at(0).content() == "new buffer");
    assert(source.size() == 0);
    assert(source.begin() == nullptr);
}

void test_scanner_clean_text() {
    SentinelScanner scanner(kSentinel);
    const std::string input = "ordinary text with no stop marker";

    auto a = scanner.feed(input.substr(0, 7));
    auto b = scanner.feed(input.substr(7));
    auto c = scanner.flush();

    assert(!a.sentinel_found);
    assert(!b.sentinel_found);
    assert(!c.sentinel_found);
    assert(a.safe_text + b.safe_text + c.safe_text == input);
}

void test_scanner_catches_sentinel_at_every_boundary() {
    const std::string text = std::string("Goodbye.") + kSentinel;

    for (std::size_t split = 0; split <= text.size(); ++split) {
        SentinelScanner scanner(kSentinel);
        auto out1 = scanner.feed(text.substr(0, split));
        auto out2 = scanner.feed(text.substr(split));

        assert(out1.sentinel_found || out2.sentinel_found);
        assert(out1.safe_text + out2.safe_text == "Goodbye.");
    }
}

void test_scanner_one_character_at_a_time() {
    const std::string text = std::string("prefix:") + kSentinel;
    SentinelScanner scanner(kSentinel);
    std::string safe;
    bool found = false;

    for (char ch : text) {
        const std::string one(1, ch);
        auto out = scanner.feed(one);
        safe += out.safe_text;
        found = found || out.sentinel_found;
    }

    assert(found);
    assert(safe == "prefix:");
}

void test_scanner_false_alarm_is_preserved() {
    SentinelScanner scanner(kSentinel);
    const std::string false_alarm = "start <|end_world|> finish";

    auto a = scanner.feed(false_alarm);
    auto b = scanner.flush();

    assert(!a.sentinel_found);
    assert(!b.sentinel_found);
    assert(a.safe_text + b.safe_text == false_alarm);
}

void test_scanner_discards_text_after_sentinel() {
    SentinelScanner scanner(kSentinel);
    auto out = scanner.feed(std::string("before") + kSentinel + "after");

    assert(out.sentinel_found);
    assert(out.safe_text == "before");
    assert(scanner.flush().safe_text.empty());
}

void test_scanner_pending_memory_is_bounded() {
    SentinelScanner scanner(kSentinel);
    const std::size_t bound = std::string(kSentinel).size() - 1;

    // Adversarial near-prefix pattern repeated many times, one byte at a time.
    const std::string pattern = "<|end_<|end_<|end_X";
    constexpr std::size_t stress_bytes = 4 * 1024 * 1024;
    std::size_t fed = 0;
    while (fed < stress_bytes) {
        for (char ch : pattern) {
            if (fed == stress_bytes) break;
            auto out = scanner.feed(std::string_view(&ch, 1));
            assert(!out.sentinel_found);
            assert(scanner.pending_.size() <= bound);
            ++fed;
        }
    }
}

void test_harness_turn_limit() {
    const auto script = temp_path("turn_limit.script");
    write_file(script,
               "role: assistant\nfirst reply\n---\n"
               "role: assistant\nsecond reply\n");

    auto model = std::make_unique<ScriptedModelClient>(script.string());
    HarnessConfig cfg;
    cfg.max_turns = 2;
    Harness harness(std::move(model), cfg);

    VectorInput input({"hello", "again"});
    StringOutput output;
    StopReason reason = harness.run(input, output);

    assert(reason.kind == StopReason::Kind::TurnLimit);
    assert(harness.conversation().size() == 4);
    assert(harness.conversation().at(3).content() == "second reply");

    std::filesystem::remove(script);
}

void test_harness_stops_on_split_sentinel() {
    const auto script = temp_path("sentinel.script");
    write_file(script,
               "chunk: 3\n"
               "role: assistant\n"
               "Goodbye!<|end_conversation|>ignored\n");

    auto model = std::make_unique<ScriptedModelClient>(script.string());
    HarnessConfig cfg;
    cfg.max_turns = 5;
    Harness harness(std::move(model), cfg);

    VectorInput input({"bye"});
    StringOutput output;
    StopReason reason = harness.run(input, output);

    assert(reason.kind == StopReason::Kind::Sentinel);
    assert(output.str().find(kSentinel) == std::string::npos);
    assert(output.str().find("ignored") == std::string::npos);
    assert(output.str().find("Goodbye!") != std::string::npos);
    assert(harness.conversation().size() == 2);
    assert(harness.conversation().at(1).content() ==
           std::string("Goodbye!") + kSentinel);

    std::filesystem::remove(script);
}

void test_harness_eof_is_clean_user_exit() {
    const auto script = temp_path("eof.script");
    write_file(script, "role: assistant\nunused\n");

    auto model = std::make_unique<ScriptedModelClient>(script.string());
    HarnessConfig cfg;
    Harness harness(std::move(model), cfg);

    VectorInput input({});
    StringOutput output;
    StopReason reason = harness.run(input, output);

    assert(reason.kind == StopReason::Kind::UserExit);
    assert(harness.conversation().size() == 0);

    std::filesystem::remove(script);
}

void test_transcript_round_trip_with_replay_client() {
    Conversation original;
    original.append(Message(Role::System, "Be concise."));
    original.append(Message(Role::User, "hello"));
    original.append(Message(Role::Assistant, "Hi there."));
    original.append(Message(Role::User, "bye"));
    original.append(Message(Role::Assistant,
                            std::string("Goodbye.") + kSentinel));

    const auto transcript = temp_path("round_trip.txt");
    save_mock_transcript(original, transcript);

    ReplayModelClient replay(transcript.string());
    assert(replay.system_message() == "Be concise.");

    Conversation replay_context;
    Message first = replay.generate(replay_context);
    Message second = replay.generate(replay_context);

    assert(first.role() == Role::Assistant);
    assert(first.content() == "Hi there.");
    assert(second.content() == std::string("Goodbye.") + kSentinel);

    std::filesystem::remove(transcript);
}

}  // namespace

int main() {
    test_message_default_and_accessors();
    test_empty_conversation_bounds();
    test_system_message_stays_first();
    test_growth_doubles_without_unnecessary_reallocation();
    test_copy_constructor_is_deep();
    test_copy_assignment_is_deep_and_self_safe();
    test_move_constructor_steals_buffer();
    test_move_assignment_steals_buffer();
    test_scanner_clean_text();
    test_scanner_catches_sentinel_at_every_boundary();
    test_scanner_one_character_at_a_time();
    test_scanner_false_alarm_is_preserved();
    test_scanner_discards_text_after_sentinel();
    test_scanner_pending_memory_is_bounded();
    test_harness_turn_limit();
    test_harness_stops_on_split_sentinel();
    test_harness_eof_is_clean_user_exit();
    test_transcript_round_trip_with_replay_client();
    return 0;
}
