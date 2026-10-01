/*
 * NcursesConsole.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include "NcursesConsole.hxx"
#include "MeshMon.hxx"
#include "MeshMonShell.hxx"
#include "MeshMonDb.hxx"
#include "version.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstring>

// --- NcursesStreamBuf Implementation ---

NcursesStreamBuf::NcursesStreamBuf(NcursesConsole &console, bool toServerLog, int colorPair)
    : _console(console),
      _toServerLog(toServerLog),
      _colorPair(colorPair) {
}

NcursesStreamBuf::int_type NcursesStreamBuf::overflow(int_type c) {
    if (c != EOF) {
        std::string line;
        bool hasLine = false;
        {
            std::lock_guard<std::mutex> lock(_bufferMutex);
            if (c == '\n') {
                line = _lineBuffer;
                _lineBuffer.clear();
                hasLine = true;
            } else if (c != '\r') {
                _lineBuffer += static_cast<char>(c);
            }
        }
        if (hasLine) {
            if (_toServerLog) {
                _console.logServer(line, _colorPair);
            } else {
                _console.logOutput(line, _colorPair, false);
            }
        }
    }
    return c;
}

int NcursesStreamBuf::sync() {
    std::string line;
    bool hasLine = false;
    {
        std::lock_guard<std::mutex> lock(_bufferMutex);
        if (!_lineBuffer.empty()) {
            line = _lineBuffer;
            _lineBuffer.clear();
            hasLine = true;
        }
    }
    if (hasLine) {
        if (_toServerLog) {
            _console.logServer(line, _colorPair);
        } else {
            _console.logOutput(line, _colorPair, false);
        }
    }
    return 0;
}

// --- NcursesConsole Implementation ---

NcursesConsole &NcursesConsole::getInstance() {
    static NcursesConsole instance;
    return instance;
}

NcursesConsole::NcursesConsole()
    : _coutBuf(*this, true, 0),
      _cerrBuf(*this, true, PAIR_WARN),
      _cmdOutputBuf(*this, false, 0) {
}

NcursesConsole::~NcursesConsole() {
    shutdown();
}

bool NcursesConsole::isRunning() const {
    return _running.load();
}

NcursesConsole::InputState NcursesConsole::getInputState() const {
    return _inputState.load();
}

std::vector<std::string> NcursesConsole::wrapText(const std::string &text, int maxCols) {
    std::vector<std::string> lines;
    if (maxCols <= 0) {
        lines.push_back(text);
        return lines;
    }

    std::string current;
    std::istringstream stream(text);
    std::string rawLine;

    while (std::getline(stream, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }

        if (rawLine.empty()) {
            lines.emplace_back("");
            continue;
        }

        std::istringstream wordStream(rawLine);
        std::string word;
        bool isFirstWordInLogicalLine = true;

        while (wordStream >> word) {
            if (isFirstWordInLogicalLine) {
                if ((int)word.size() > maxCols) {
                    size_t pos = 0;
                    while (pos < word.size()) {
                        size_t chunk = std::min((size_t)maxCols, word.size() - pos);
                        if (pos == 0) {
                            lines.push_back(word.substr(pos, chunk));
                        } else {
                            std::string cont = "  " + word.substr(pos, chunk > 2 ? chunk - 2 : chunk);
                            lines.push_back(cont);
                        }
                        pos += chunk;
                    }
                    current.clear();
                } else {
                    current = word;
                }
                isFirstWordInLogicalLine = false;
            } else {
                if ((int)(current.size() + 1 + word.size()) <= maxCols) {
                    current += " " + word;
                } else {
                    lines.push_back(current);
                    if ((int)word.size() > maxCols - 2) {
                        current = "  " + word.substr(0, std::max(1, maxCols - 2));
                    } else {
                        current = "  " + word;
                    }
                }
            }
        }
        if (!current.empty()) {
            lines.push_back(current);
            current.clear();
        }
    }

    if (lines.empty()) {
        lines.emplace_back("");
    }
    return lines;
}

NcursesConsole::LayoutBudget NcursesConsole::calculateLayout(int termRows) {
    LayoutBudget b;
    b.headerRows = 1;
    b.logSepRows = 1;
    b.logRows = (termRows >= 24) ? 4 : 2;
    b.midSepRows = 1;
    b.bottomSepRows = 1;
    b.inputRows = 1;
    b.cmdHeight = termRows - b.headerRows - b.logSepRows - b.logRows - b.midSepRows - b.bottomSepRows - b.inputRows;
    if (b.cmdHeight < 3) b.cmdHeight = 3;
    return b;
}

bool NcursesConsole::init() {
    std::lock_guard<std::recursive_mutex> lock(_uiMutex);
    if (_initialized.load()) {
        return true;
    }

    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    nonl();
    set_escdelay(25);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(PAIR_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(PAIR_PROMPT, COLOR_GREEN, -1);
        init_pair(PAIR_INFO, COLOR_CYAN, -1);
        init_pair(PAIR_WARN, COLOR_YELLOW, -1);
        init_pair(PAIR_ERROR, COLOR_RED, -1);
        init_pair(PAIR_TEXT, COLOR_WHITE, -1);
        init_pair(PAIR_MUTED, COLOR_WHITE, -1);
    }

    setupWindows();
    _initialized = true;
    _running = true;

    // Redirect stdout and stderr so daemon server logs go strictly to the top log pane
    _oldCoutBuf = std::cout.rdbuf(&_coutBuf);
    _oldCerrBuf = std::cerr.rdbuf(&_cerrBuf);

    return true;
}

void NcursesConsole::setupWindows() {
    getmaxyx(stdscr, _termRows, _termCols);

    if (_termRows < 12) _termRows = 12;
    if (_termCols < 40) _termCols = 40;

    auto b = calculateLayout(_termRows);
    _logHeight = b.logRows;
    _cmdHeight = b.cmdHeight;

    // 1. Header window (Row 0)
    _headerWin = newwin(b.headerRows, _termCols, 0, 0);

    // 2. Server log separator (Row 1)
    _logSepWin = newwin(b.logSepRows, _termCols, 1, 0);

    // 3. Server logs window (Rows 2 to 2 + _logHeight - 1)
    _logWin = newwin(b.logRows, _termCols, 2, 0);
    scrollok(_logWin, FALSE);

    // 4. Middle divider with title & scroll badge (Row 2 + _logHeight)
    int midSepRow = 2 + _logHeight;
    _midSepWin = newwin(b.midSepRows, _termCols, midSepRow, 0);

    // 5. Command outputs panel (Rows midSepRow + 1 to midSepRow + _cmdHeight)
    int cmdRow = midSepRow + 1;
    _cmdWin = newwin(_cmdHeight, _termCols, cmdRow, 0);
    scrollok(_cmdWin, FALSE);

    // 6. Bottom divider (Row _termRows - 2)
    _bottomSepWin = newwin(b.bottomSepRows, _termCols, _termRows - 2, 0);
    std::string botSep(_termCols > 1 ? _termCols - 1 : 1, '-');
    mvwprintw(_bottomSepWin, 0, 0, "%s", botSep.c_str());
    wrefresh(_bottomSepWin);

    // 7. Input line (Row _termRows - 1)
    _inputWin = newwin(b.inputRows, _termCols, _termRows - 1, 0);
    scrollok(_inputWin, FALSE);
    idlok(_inputWin, FALSE);
    wtimeout(_inputWin, 100);
    keypad(_inputWin, TRUE);

    // Initial banner output
    if (_cmdHistory.empty()) {
        addOutputLine("--- Welcome to MeshMon Console ---", PAIR_INFO, true);
        addOutputLine("Type 'help' for commands, 'status' for mesh info, 'quit' to exit.", PAIR_INFO, false);
        addOutputLine("", 0, false);
    }

    updateHeader();
    renderLogPanel();
    renderMiddlePanel();
    redrawInputLine();
}

void NcursesConsole::destroyWindows() {
    if (_headerWin) { delwin(_headerWin); _headerWin = nullptr; }
    if (_logSepWin) { delwin(_logSepWin); _logSepWin = nullptr; }
    if (_logWin) { delwin(_logWin); _logWin = nullptr; }
    if (_midSepWin) { delwin(_midSepWin); _midSepWin = nullptr; }
    if (_cmdWin) { delwin(_cmdWin); _cmdWin = nullptr; }
    if (_bottomSepWin) { delwin(_bottomSepWin); _bottomSepWin = nullptr; }
    if (_inputWin) { delwin(_inputWin); _inputWin = nullptr; }
}

void NcursesConsole::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(_uiMutex);
    if (!_initialized.load()) {
        return;
    }

    _running = false;
    _initialized = false;

    if (_oldCoutBuf) {
        std::cout.rdbuf(_oldCoutBuf);
        _oldCoutBuf = nullptr;
    }
    if (_oldCerrBuf) {
        std::cerr.rdbuf(_oldCerrBuf);
        _oldCerrBuf = nullptr;
    }

    if (_cmdWorker && _cmdWorker->joinable()) {
        _cmdWorker->join();
    }
    _cmdWorker.reset();

    destroyWindows();
    endwin();
}

void NcursesConsole::handleResize() {
    endwin();
    refresh();
    destroyWindows();
    setupWindows();
}

void NcursesConsole::setShell(std::shared_ptr<MeshMonShell> shell) {
    _shell = shell;
    if (_shell) {
        _shell->setNcursesMode(true);
    }
}

void NcursesConsole::setMon(std::shared_ptr<MeshMon> mon) {
    _mon = mon;
}

void NcursesConsole::setDb(std::shared_ptr<MeshMonDb> db) {
    _db = db;
}

void NcursesConsole::setShutdownCallback(ShutdownCallback cb) {
    _shutdownCb = cb;
}

void NcursesConsole::updateHeader() {
    if (!_headerWin) return;

    std::ostringstream ss;
    ss << " MeshMon v" << MYPROJECT_VERSION_STRING << " | ";

    if (_mon && _mon->isConnected()) {
        ss << "Radio: [CONNECTED] | ";
    } else {
        ss << "Radio: [CONNECTING] | ";
    }

    if (_db) {
        ss << "DB: [OK] | ";
    }

    std::string text = ss.str();
    if ((int)text.size() < _termCols) {
        text.append(_termCols - text.size(), ' ');
    } else {
        text = text.substr(0, _termCols);
    }

    wattron(_headerWin, COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvwprintw(_headerWin, 0, 0, "%s", text.c_str());
    wattroff(_headerWin, COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    wrefresh(_headerWin);
}

void NcursesConsole::addOutputLine(const std::string &line, int colorPair, bool isBold) {
    OutputLine item;
    item.text = line;
    item.colorPair = colorPair;
    item.isBold = isBold;
    _cmdHistory.push_back(item);
    if (_cmdHistory.size() > MAX_HISTORY_LINES) {
        _cmdHistory.pop_front();
    }
}

void NcursesConsole::logOutput(const std::string &text, int colorPair, bool isBold) {
    std::lock_guard<std::recursive_mutex> lock(_uiMutex);
    int maxCols = _termCols > 1 ? _termCols - 1 : 40;
    auto wrapped = wrapText(text, maxCols);
    for (const auto &w : wrapped) {
        addOutputLine(w, colorPair, isBold);
    }
    renderMiddlePanel();
    redrawInputLine();
}

void NcursesConsole::logServer(const std::string &text, int colorPair) {
    std::lock_guard<std::recursive_mutex> lock(_uiMutex);
    int maxCols = _termCols > 1 ? _termCols - 1 : 40;
    auto wrapped = wrapText(text, maxCols);
    for (const auto &w : wrapped) {
        LogLine item;
        item.text = w;
        item.colorPair = colorPair;
        _serverLogHistory.push_back(item);
        if (_serverLogHistory.size() > MAX_SERVER_LOG_LINES) {
            _serverLogHistory.pop_front();
        }
    }
    renderLogPanel();
    redrawInputLine();
}

void NcursesConsole::renderLogPanel() {
    if (!_logWin || !_logSepWin) return;

    // Redraw log separator banner
    std::string sepTitle = "--- [ Live Mesh Radio & Telemetry Log ] ";
    if ((int)sepTitle.size() < _termCols) {
        sepTitle.append(_termCols - sepTitle.size(), '-');
    } else {
        sepTitle = sepTitle.substr(0, _termCols);
    }
    wattron(_logSepWin, COLOR_PAIR(PAIR_MUTED));
    mvwprintw(_logSepWin, 0, 0, "%s", sepTitle.c_str());
    wattroff(_logSepWin, COLOR_PAIR(PAIR_MUTED));
    wrefresh(_logSepWin);

    werase(_logWin);
    int totalLogs = (int)_serverLogHistory.size();
    int startIdx = std::max(0, totalLogs - _logHeight);

    for (int i = 0; i < _logHeight && (startIdx + i) < totalLogs; ++i) {
        const auto &log = _serverLogHistory[startIdx + i];
        if (log.colorPair != 0) {
            wattron(_logWin, COLOR_PAIR(log.colorPair));
        }
        mvwprintw(_logWin, i, 0, "%s", log.text.c_str());
        if (log.colorPair != 0) {
            wattroff(_logWin, COLOR_PAIR(log.colorPair));
        }
    }
    wrefresh(_logWin);
}

void NcursesConsole::renderMiddlePanel() {
    if (!_cmdWin || !_midSepWin) return;

    // Middle Divider Title & Scroll badge
    std::string midTitle = "--- [ Interactive Shell Output ] ";
    if (_scrollOffset > 0) {
        midTitle += "[Scroll: +" + std::to_string(_scrollOffset) + "] ";
    }
    if ((int)midTitle.size() < _termCols) {
        midTitle.append(_termCols - midTitle.size(), '-');
    } else {
        midTitle = midTitle.substr(0, _termCols);
    }
    wattron(_midSepWin, COLOR_PAIR(PAIR_MUTED));
    mvwprintw(_midSepWin, 0, 0, "%s", midTitle.c_str());
    wattroff(_midSepWin, COLOR_PAIR(PAIR_MUTED));
    wrefresh(_midSepWin);

    werase(_cmdWin);
    int totalLines = (int)_cmdHistory.size();
    int visibleLines = _cmdHeight;

    int effectiveEnd = totalLines - _scrollOffset;
    if (effectiveEnd < 0) effectiveEnd = 0;
    int startIdx = effectiveEnd - visibleLines;
    if (startIdx < 0) startIdx = 0;

    int renderCount = std::min(visibleLines, effectiveEnd - startIdx);
    for (int i = 0; i < renderCount; ++i) {
        int idx = startIdx + i;
        if (idx >= 0 && idx < totalLines) {
            const auto &line = _cmdHistory[idx];
            int attrs = 0;
            if (line.colorPair != 0) attrs |= COLOR_PAIR(line.colorPair);
            if (line.isBold) attrs |= A_BOLD;
            if (attrs != 0) wattron(_cmdWin, attrs);

            mvwprintw(_cmdWin, i, 0, "%s", line.text.c_str());

            if (attrs != 0) wattroff(_cmdWin, attrs);
        }
    }
    wrefresh(_cmdWin);
}

void NcursesConsole::redrawInputLine() {
    if (!_inputWin) return;

    werase(_inputWin);

    if (_inputState.load() == InputState::EXECUTING) {
        std::string execMsg = "[running: " + _activeCommandName + "... press Ctrl+C to cancel]";
        wattron(_inputWin, COLOR_PAIR(PAIR_WARN) | A_BOLD);
        mvwprintw(_inputWin, 0, 0, "%s", execMsg.c_str());
        wattroff(_inputWin, COLOR_PAIR(PAIR_WARN) | A_BOLD);
        curs_set(0);
    } else {
        std::string prompt = "meshmon> ";
        wattron(_inputWin, COLOR_PAIR(PAIR_PROMPT) | A_BOLD);
        mvwprintw(_inputWin, 0, 0, "%s", prompt.c_str());
        wattroff(_inputWin, COLOR_PAIR(PAIR_PROMPT) | A_BOLD);

        mvwprintw(_inputWin, 0, prompt.size(), "%s", _inputBuffer.c_str());
        curs_set(1);
        wmove(_inputWin, 0, prompt.size() + _inputBuffer.size());
    }

    wrefresh(_inputWin);
}

void NcursesConsole::processCommand(const std::string &line) {
    if (line.empty()) {
        return;
    }

    if (_commandHistoryRing.empty() || _commandHistoryRing.back() != line) {
        _commandHistoryRing.push_back(line);
    }
    _historyRingIdx = -1;

    addOutputLine("meshmon> " + line, PAIR_PROMPT, false);

    std::string trimmed = line;
    size_t first = trimmed.find_first_not_of(" \t");
    if (first != std::string::npos) {
        trimmed = trimmed.substr(first);
    }
    size_t last = trimmed.find_last_not_of(" \t");
    if (last != std::string::npos) {
        trimmed = trimmed.substr(0, last + 1);
    }

    if (trimmed == "clear") {
        _cmdHistory.clear();
        _scrollOffset = 0;
        addOutputLine("History cleared.", PAIR_INFO, false);
        addOutputLine("", 0, false);
        renderMiddlePanel();
        redrawInputLine();
        return;
    } else if (trimmed == "quit" || trimmed == "exit") {
        addOutputLine("Shutting down MeshMon daemon...", PAIR_INFO, true);
        renderMiddlePanel();
        _running = false;
        if (_shutdownCb) {
            _shutdownCb();
        }
        return;
    }

    // Set state to EXECUTING and spawn background worker thread
    _inputState.store(InputState::EXECUTING);
    _activeCommandName = trimmed;
    _workerFinished.store(false);
    _scrollOffset = 0;
    renderMiddlePanel();
    redrawInputLine();

    if (_cmdWorker && _cmdWorker->joinable()) {
        _cmdWorker->join();
    }
    _cmdWorker = std::make_unique<std::thread>([this, line]() {
        auto *prevCout = std::cout.rdbuf(&_cmdOutputBuf);
        auto *prevCerr = std::cerr.rdbuf(&_cmdOutputBuf);
        if (_shell) {
            _shell->executeCommand(line);
        }
        std::cout << std::flush;
        std::cerr << std::flush;
        std::cout.rdbuf(prevCout);
        std::cerr.rdbuf(prevCerr);
        _workerExitCode = 0;
        _workerFinished.store(true);
    });
}

void NcursesConsole::run() {
    int headerTick = 0;

    while (_running.load()) {
        if (_workerFinished.load()) {
            if (_cmdWorker && _cmdWorker->joinable()) {
                _cmdWorker->join();
            }
            _cmdWorker.reset();
            _workerFinished.store(false);
            _inputState.store(InputState::READY);
            _activeCommandName.clear();

            flushinp();
            _inputBuffer.clear();

            std::lock_guard<std::recursive_mutex> lock(_uiMutex);
            _scrollOffset = 0;
            renderMiddlePanel();
            updateHeader();
            redrawInputLine();
        }

        bool wasExecuting = (_inputState.load() == InputState::EXECUTING);
        int ch = wgetch(_inputWin);

        if (_workerFinished.load()) {
            if (_cmdWorker && _cmdWorker->joinable()) {
                _cmdWorker->join();
            }
            _cmdWorker.reset();
            _workerFinished.store(false);
            _inputState.store(InputState::READY);
            _activeCommandName.clear();

            flushinp();
            _inputBuffer.clear();

            std::lock_guard<std::recursive_mutex> lock(_uiMutex);
            _scrollOffset = 0;
            renderMiddlePanel();
            updateHeader();
            redrawInputLine();
        }

        if (ch == ERR) {
            if (++headerTick >= 20) { // Update header every ~2s
                headerTick = 0;
                std::lock_guard<std::recursive_mutex> lock(_uiMutex);
                updateHeader();
                redrawInputLine();
            }
            continue;
        }

        if (ch == KEY_RESIZE) {
            std::lock_guard<std::recursive_mutex> lock(_uiMutex);
            handleResize();
            continue;
        }

        std::lock_guard<std::recursive_mutex> lock(_uiMutex);

        if (wasExecuting) {
            if (ch == 3) { // Ctrl+C
                _inputState.store(InputState::READY);
                _activeCommandName.clear();
                redrawInputLine();
            }
            continue;
        }

        switch (ch) {
        case '\n':
        case '\r':
        case KEY_ENTER: {
            std::string line = _inputBuffer;
            _inputBuffer.clear();
            processCommand(line);
            break;
        }
        case 3: // Ctrl+C
            _inputBuffer.clear();
            _historyRingIdx = -1;
            addOutputLine("^C", PAIR_MUTED, false);
            renderMiddlePanel();
            redrawInputLine();
            break;
        case 4: // Ctrl+D (EOF)
            if (_inputBuffer.empty()) {
                _running = false;
                if (_shutdownCb) {
                    _shutdownCb();
                }
            }
            break;
        case 12: // Ctrl+L (Redraw / Clear screen)
            handleResize();
            break;
        case KEY_BACKSPACE:
        case 127:
        case '\b':
            if (!_inputBuffer.empty()) {
                _inputBuffer.pop_back();
                redrawInputLine();
            }
            break;
        case KEY_UP:
            if (!_commandHistoryRing.empty()) {
                if (_historyRingIdx == -1) {
                    _historyRingIdx = (int)_commandHistoryRing.size() - 1;
                } else if (_historyRingIdx > 0) {
                    _historyRingIdx--;
                }
                if (_historyRingIdx >= 0 && _historyRingIdx < (int)_commandHistoryRing.size()) {
                    _inputBuffer = _commandHistoryRing[_historyRingIdx];
                    redrawInputLine();
                }
            }
            break;
        case KEY_DOWN:
            if (!_commandHistoryRing.empty() && _historyRingIdx != -1) {
                if (_historyRingIdx < (int)_commandHistoryRing.size() - 1) {
                    _historyRingIdx++;
                    _inputBuffer = _commandHistoryRing[_historyRingIdx];
                } else {
                    _historyRingIdx = -1;
                    _inputBuffer.clear();
                }
                redrawInputLine();
            }
            break;
        case KEY_PPAGE: { // Page Up -> Scroll Up in command output
            int maxOffset = std::max(0, (int)_cmdHistory.size() - _cmdHeight);
            _scrollOffset = std::min(maxOffset, _scrollOffset + 5);
            renderMiddlePanel();
            redrawInputLine();
            break;
        }
        case KEY_NPAGE: { // Page Down -> Scroll Down in command output
            _scrollOffset = std::max(0, _scrollOffset - 5);
            renderMiddlePanel();
            redrawInputLine();
            break;
        }
        default:
            if (isprint(ch) && _inputBuffer.size() < 256) {
                _inputBuffer += static_cast<char>(ch);
                redrawInputLine();
            }
            break;
        }
    }
}

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
