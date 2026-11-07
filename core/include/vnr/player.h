// Runs a compiled Program in real time: stage changes, waits, typewriter, choices, Back, backlog.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "vnr/program.h"
#include "vnr/stage.h"

namespace vnr {

struct Settings {
    double cps = 45;                 // typewriter: 22 slow, 45 normal, 90 fast, 0 instant
    bool auto_mode = false;
    double auto_seconds = 2.6;       // per line in auto mode
    bool auto_waits_for_voice = true;
    double voice_wait_cap = 12;      // [wait voice] never blocks longer than this
    TextContext text;                // master name and gender
};

// Game-progress questions asked by branch commands. Defaults: everything cleared/owned.
class Conditions {
public:
    virtual ~Conditions() = default;
    virtual bool quest_cleared(std::string_view /*quest_id*/) { return true; }
    virtual bool has_material(std::string_view /*material_id*/) { return true; }
    virtual bool revival() { return true; }
};

struct AudioEvent {
    enum class Kind { BgmPlay, BgmStop, SePlay, SeStop, SeVolume, SeStopAll, VoicePlay, VoiceStop, Movie };
    Kind kind = Kind::SePlay;
    std::string name;    // bgm / se / voice id ("A_B") / movie id
    std::string folder;  // cueSe folder, or tVoice folder (then `name` is the file)
    double fade = 0;     // fade-in (play) or fade-out (stop) seconds
    float volume = 1;
    bool loop = false;
};

struct BacklogEntry {
    RichText speaker;
    RichText text;
    std::string voice_name, voice_folder;  // empty when the line has no voice
    int line = 0;
};

enum class Status {
    Running,       // executing instructions (transient)
    Waiting,       // [wt] / [wait X] / movie
    Typing,        // dialogue being revealed
    AwaitAdvance,  // dialogue fully shown, waiting for the reader
    AwaitChoice,   // options shown
    Ended,
};

struct TextView {
    const SayOp* say = nullptr;  // null = no text box
    std::size_t shown = 0;       // glyphs revealed
    bool visible = false;
};

class Player {
public:
    Player(const Program& program, Settings settings = {}, Conditions* conditions = nullptr);

    void update(double dt);  // advance time
    bool advance();          // reader tap: finish typing, or continue
    bool choose(std::size_t option_index);
    bool back();             // return to the previous dialogue or choice
    void skip();             // jump to the end of this script ("Skip scene")
    void voice_finished();   // the app reports the current voice ended
    void movie_finished();   // the app reports the movie ended (or was skipped)
    // Load a save: fast-forward a fresh Player (muted, no waits) to the stop at `stop_pc` (a dialogue or a choice),
    // taking `choices` (option indices, in order) at the choices on the way. The line is then shown in full and the
    // BGM of that point is (re)started. Back and the backlog work as if the reader had read up to there.
    // Returns false if the stop is not reached; the Player is then unusable and must be recreated.
    bool resume(std::size_t stop_pc, const std::vector<int>& choices);

    Status status() const { return status_; }
    const Stage& stage() const { return stage_; }
    TextView text() const;
    const ChoiceOp* pending_choice() const;
    const std::vector<BacklogEntry>& backlog() const { return backlog_; }
    std::vector<AudioEvent> drain_audio();
    int line() const { return line_; }           // script line of the last instruction run
    std::size_t pc() const { return pc_; }
    const std::string& bgm() const { return bgm_; }
    const std::vector<int>& choice_path() const { return choices_; }  // options taken in this script so far
    Settings& settings() { return settings_; }

private:
    struct Snapshot {
        std::size_t pc;
        Stage stage;
        std::string bgm;
        std::size_t backlog_size;
        std::size_t choices_size;
    };

    const Program& prog_;
    Settings settings_;
    Conditions default_conditions_;
    Conditions* cond_;

    Stage stage_;
    std::size_t pc_ = 0;
    Status status_ = Status::Running;
    int line_ = 0;

    // waiting
    double wait_timer_ = 0;
    WaitCat wait_cat_ = WaitCat::None;
    bool waiting_movie_ = false;

    // dialogue
    const SayOp* say_ = nullptr;
    std::vector<double> reveal_;
    double typed_ = 0;
    std::size_t shown_ = 0;
    std::size_t next_inline_ = 0;
    double inline_wait_ = 0;
    WaitCat inline_cat_ = WaitCat::None;
    double auto_timer_ = 0;

    // audio
    std::string bgm_;
    std::string unclaimed_voice_;  // [voice] played since the last step that took time: the next line's voice
    bool voice_active_ = false;
    bool mute_events_ = false;
    std::vector<AudioEvent> audio_;

    std::vector<Snapshot> history_;
    std::vector<int> choices_;
    std::vector<BacklogEntry> backlog_;

    void run();
    void begin_say(const SayOp& op);
    void finish_typing(bool skipping);
    void step_typing(double dt);
    void stop_voice();
    void exec(const Command& c, bool inline_cmd, bool skipping);
    void start_wait(double seconds, WaitCat cat);
    void update_talk_focus(const std::string& speaker_code, const std::string& speaker_name);
    void emit(AudioEvent e);
    bool eval(const CondJumpOp& op);
};

}  // namespace vnr
