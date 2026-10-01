/*
 * NcursesConsole.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef MESHMON_NCURSES_CONSOLE_HXX
#define MESHMON_NCURSES_CONSOLE_HXX

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <functional>
#include <iostream>
#include <streambuf>
#include <thread>
#include <memory>
#include <curses.h>

class NcursesConsole;
class MeshMon;
class MeshMonShell;
class MeshMonDb;

class NcursesStreamBuf : public std::streambuf {
public:
    explicit NcursesStreamBuf(NcursesConsole &console, bool toServerLog = true, int colorPair = 0);
    ~NcursesStreamBuf() override = default;

protected:
    int_type overflow(int_type c) override;
    int sync() override;

private:
    NcursesConsole &_console;
    bool _toServerLog;
    int _colorPair;
    std::string _lineBuffer;
    std::mutex _bufferMutex;
};

class NcursesConsole {
public:
    enum ColorPairs {
        PAIR_HEADER = 1,
        PAIR_PROMPT = 2,
        PAIR_INFO = 3,
        PAIR_WARN = 4,
        PAIR_ERROR = 5,
        PAIR_TEXT = 6,
        PAIR_MUTED = 7
    };

    using ShutdownCallback = std::function<void()>;

    enum class InputState {
        READY,
        EXECUTING
    };

    static NcursesConsole &getInstance();

    NcursesConsole();
    ~NcursesConsole();

    bool init();
    void shutdown();

    bool isRunning() const;
    InputState getInputState() const;
    void run();

    void setShell(std::shared_ptr<MeshMonShell> shell);
    void setMon(std::shared_ptr<MeshMon> mon);
    void setDb(std::shared_ptr<MeshMonDb> db);
    void setShutdownCallback(ShutdownCallback cb);

    // Writes to main middle panel (Interactive Command Outputs)
    void logOutput(const std::string &text, int colorPair = 0, bool isBold = false);

    // Writes to top log pane (Server stdout/stderr logs & Packet Streams)
    void logServer(const std::string &text, int colorPair = 0);

    void updateHeader();

    // Deterministic word-boundary line wrapping
    static std::vector<std::string> wrapText(const std::string &text, int maxCols);

    // Terminal geometry calculation
    struct LayoutBudget {
        int headerRows;
        int logSepRows;
        int logRows;
        int midSepRows;
        int cmdHeight;
        int bottomSepRows;
        int inputRows;
    };
    static LayoutBudget calculateLayout(int termRows);

private:
    struct OutputLine {
        std::string text;
        int colorPair = 0;
        bool isBold = false;
    };
    struct LogLine {
        std::string text;
        int colorPair = 0;
    };
    static constexpr size_t MAX_HISTORY_LINES = 1000;
    static constexpr size_t MAX_SERVER_LOG_LINES = 100;

    void setupWindows();
    void destroyWindows();
    void handleResize();
    void processCommand(const std::string &line);
    void redrawInputLine();
    void renderMiddlePanel();
    void renderLogPanel();
    void addOutputLine(const std::string &line, int colorPair = 0, bool isBold = false);

    ShutdownCallback _shutdownCb;
    std::shared_ptr<MeshMonShell> _shell;
    std::shared_ptr<MeshMon> _mon;
    std::shared_ptr<MeshMonDb> _db;

    std::recursive_mutex _uiMutex;
    std::atomic<bool> _running{false};
    std::atomic<bool> _initialized{false};

    WINDOW *_headerWin = nullptr;
    WINDOW *_logSepWin = nullptr;
    WINDOW *_logWin = nullptr;
    WINDOW *_midSepWin = nullptr;
    WINDOW *_cmdWin = nullptr;
    WINDOW *_bottomSepWin = nullptr;
    WINDOW *_inputWin = nullptr;

    std::string _inputBuffer;
    int _termRows = 0;
    int _termCols = 0;
    int _logHeight = 0;
    int _cmdHeight = 0;

    std::deque<OutputLine> _cmdHistory;
    std::deque<LogLine> _serverLogHistory;
    std::vector<std::string> _commandHistoryRing;
    int _historyRingIdx = -1;
    int _scrollOffset = 0;

    NcursesStreamBuf _coutBuf;
    NcursesStreamBuf _cerrBuf;
    NcursesStreamBuf _cmdOutputBuf;
    std::streambuf *_oldCoutBuf = nullptr;
    std::streambuf *_oldCerrBuf = nullptr;

    std::atomic<InputState> _inputState{InputState::READY};
    std::unique_ptr<std::thread> _cmdWorker;
    std::atomic<bool> _workerFinished{false};
    std::string _activeCommandName;
    int _workerExitCode = 0;
};

#endif /* MESHMON_NCURSES_CONSOLE_HXX */

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
