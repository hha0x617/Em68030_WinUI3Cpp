#include "pch.h"

#include "Vt100Terminal.h"

#include <algorithm>
#include <sstream>

namespace Em68030::Views {

// ============================================================================
// VT100 line drawing map (ASCII -> Unicode code point)
// ============================================================================

const std::unordered_map<char, char32_t> Vt100Terminal::s_lineDrawingMap = {
    {'j', U'\u2518'}, // box drawings light up and left
    {'k', U'\u2510'}, // box drawings light down and left
    {'l', U'\u250C'}, // box drawings light down and right
    {'m', U'\u2514'}, // box drawings light up and right
    {'n', U'\u253C'}, // box drawings light vertical and horizontal
    {'q', U'\u2500'}, // box drawings light horizontal
    {'t', U'\u251C'}, // box drawings light vertical and right
    {'u', U'\u2524'}, // box drawings light vertical and left
    {'v', U'\u2534'}, // box drawings light up and horizontal
    {'w', U'\u252C'}, // box drawings light down and horizontal
    {'x', U'\u2502'}, // box drawings light vertical
    {'a', U'\u2592'}, // medium shade
    {'f', U'\u00B0'}, // degree sign
    {'g', U'\u00B1'}, // plus-minus sign
    {'~', U'\u00B7'}, // middle dot
    {'y', U'\u2264'}, // less-than or equal to
    {'z', U'\u2265'}, // greater-than or equal to
};

// ============================================================================
// UTF-8 encoding helper
// ============================================================================

void Vt100Terminal::AppendUtf8(std::string& out, char32_t cp)
{
    if (cp <= 0x7F)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0x10FFFF)
    {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// ============================================================================
// Constructor
// ============================================================================

Vt100Terminal::Vt100Terminal(int cols, int rows, int maxScrollback)
    : m_cols(cols)
    , m_rows(rows)
    , m_maxScrollback(std::clamp(maxScrollback, 0, 100000))
    , m_scrollBottom(rows - 1)
{
    m_scrollback.resize(m_maxScrollback > 0 ? m_maxScrollback : 1);
    m_screen.resize(rows * cols, ' ');
}

// ============================================================================
// ResizeScrollback
// ============================================================================

void Vt100Terminal::ResizeScrollback(int newMax)
{
    newMax = std::clamp(newMax, 0, 100000);
    if (newMax == m_maxScrollback) return;

    std::vector<std::string> newBuf(newMax > 0 ? newMax : 1);
    if (newMax > 0 && m_scrollbackCount > 0)
    {
        int copyCount = std::min(m_scrollbackCount, newMax);
        int srcStart = (m_scrollbackHead + m_scrollbackCount - copyCount) % m_maxScrollback;
        for (int i = 0; i < copyCount; i++)
            newBuf[i] = std::move(m_scrollback[(srcStart + i) % m_maxScrollback]);
        m_scrollbackHead = 0;
        m_scrollbackCount = copyCount;
    }
    else
    {
        m_scrollbackHead = 0;
        m_scrollbackCount = 0;
    }

    m_scrollback = std::move(newBuf);
    m_maxScrollback = newMax;
    m_dirty = true;
}

// ============================================================================
// Write
// ============================================================================

void Vt100Terminal::Write(char ch)
{
    switch (m_state)
    {
        case State::Normal:
            ProcessNormal(ch);
            break;
        case State::Esc:
            ProcessEsc(ch);
            break;
        case State::Csi:
            ProcessCsi(ch);
            break;
        case State::EscParen:
            // ESC ( or ESC ) -- character set designation, consume one more char
            m_state = State::Normal;
            break;
    }
}

void Vt100Terminal::Write(const std::string& s)
{
    for (char ch : s)
        Write(ch);
}

// ============================================================================
// Render
// ============================================================================

std::string Vt100Terminal::Render() const
{
    std::string result;
    result.reserve(m_rows * (m_cols * 3 + 1)); // Up to 3 bytes per char (UTF-8) + newlines
    for (int r = 0; r < m_rows; r++)
    {
        if (r > 0) result.push_back('\n');
        for (int c = 0; c < m_cols; c++)
        {
            char ch = ScreenAt(r, c);
            // Screen cells are stored as plain chars; line-drawing characters
            // are already resolved in PutChar, but since we store them as char
            // (which can only hold ASCII), we just output the char directly.
            result.push_back(ch);
        }
    }
    return result;
}

// ============================================================================
// RenderWithCursor
// ============================================================================

std::string Vt100Terminal::RenderWithCursor() const
{
    std::string result;
    result.reserve(m_rows * (m_cols * 3 + 1));
    for (int r = 0; r < m_rows; r++)
    {
        if (r > 0) result.push_back('\n');
        for (int c = 0; c < m_cols; c++)
        {
            if (r == m_cursorRow && c == m_cursorCol)
            {
                // Full block cursor U+2588 (UTF-8: E2 96 88)
                AppendUtf8(result, U'\u2588');
            }
            else
            {
                result.push_back(ScreenAt(r, c));
            }
        }
    }
    return result;
}

// ============================================================================
// RenderFull
// ============================================================================

std::string Vt100Terminal::RenderFull() const
{
    std::string result;
    result.reserve(m_scrollbackCount * (m_cols + 1) + m_rows * (m_cols + 1));

    for (int i = 0; i < m_scrollbackCount; i++)
    {
        result.append(m_scrollback[(m_scrollbackHead + i) % m_maxScrollback]);
        result.push_back('\n');
    }

    for (int r = 0; r < m_rows; r++)
    {
        if (r > 0) result.push_back('\n');
        for (int c = 0; c < m_cols; c++)
            result.push_back(ScreenAt(r, c));
    }
    return result;
}

// ============================================================================
// Normal character processing
// ============================================================================

void Vt100Terminal::ProcessNormal(char ch)
{
    switch (ch)
    {
        case '\x1B': // ESC
            m_state = State::Esc;
            break;
        case '\r': // CR
            m_cursorCol = 0;
            m_dirty = true;
            break;
        case '\n': // LF -- also do CR (the write bypass skips tty ONLCR processing)
            m_cursorCol = 0;
            LineFeed();
            break;
        case '\b': // BS
            if (m_cursorCol > 0) m_cursorCol--;
            m_dirty = true;
            break;
        case '\t': // TAB
            m_cursorCol = std::min((m_cursorCol / 8 + 1) * 8, m_cols - 1);
            m_dirty = true;
            break;
        case '\x07': // BEL
            break;
        case '\x0E': // SO -- Switch to alternate character set
            m_alternateCharset = true;
            break;
        case '\x0F': // SI -- Switch to standard character set
            m_alternateCharset = false;
            break;
        default:
            if (ch >= ' ')
                PutChar(ch);
            break;
    }
}

void Vt100Terminal::PutChar(char ch)
{
    // Map line-drawing characters when in alternate charset
    // Since we store char (not wchar_t), line-drawing Unicode chars cannot be
    // stored directly in the screen buffer. We store a placeholder '?' for
    // line drawing in the cell and handle rendering separately.
    // However, for simplicity and faithful port: we keep the char buffer and
    // let the Render methods output plain chars. If you need full Unicode
    // line drawing in the screen buffer, switch m_screen to std::vector<char32_t>.
    if (m_alternateCharset)
    {
        auto it = s_lineDrawingMap.find(ch);
        if (it != s_lineDrawingMap.end())
        {
            // Store a substitute ASCII approximation in the cell
            // (Full Unicode rendering is handled at the UI layer)
            switch (ch)
            {
                case 'j': ch = '+'; break; // corner
                case 'k': ch = '+'; break; // corner
                case 'l': ch = '+'; break; // corner
                case 'm': ch = '+'; break; // corner
                case 'n': ch = '+'; break; // cross
                case 'q': ch = '-'; break; // horizontal
                case 't': ch = '+'; break; // tee
                case 'u': ch = '+'; break; // tee
                case 'v': ch = '+'; break; // tee
                case 'w': ch = '+'; break; // tee
                case 'x': ch = '|'; break; // vertical
                case 'a': ch = '#'; break; // shade
                case 'f': ch = 'o'; break; // degree
                case 'g': ch = '+'; break; // plus-minus
                case '~': ch = '.'; break; // middle dot
                case 'y': ch = '<'; break; // less-equal
                case 'z': ch = '>'; break; // greater-equal
                default: break;
            }
        }
    }

    if (m_cursorCol >= m_cols)
    {
        // Auto-wrap
        m_cursorCol = 0;
        LineFeed();
    }

    ScreenAt(m_cursorRow, m_cursorCol) = ch;
    m_cursorCol++;
    m_dirty = true;
}

void Vt100Terminal::LineFeed()
{
    if (m_cursorRow == m_scrollBottom)
        ScrollUp(1);
    else if (m_cursorRow < m_rows - 1)
        m_cursorRow++;
    m_dirty = true;
}

// ============================================================================
// ESC sequence processing
// ============================================================================

void Vt100Terminal::ProcessEsc(char ch)
{
    switch (ch)
    {
        case '[': // CSI
            m_state = State::Csi;
            m_csiParams.clear();
            m_currentParam = 0;
            m_hasCurrentParam = false;
            m_csiQuestion = false;
            break;
        case '(':
        case ')': // Character set designation
            m_state = State::EscParen;
            break;
        case '7': // DECSC -- Save cursor
            m_savedRow = m_cursorRow;
            m_savedCol = m_cursorCol;
            m_state = State::Normal;
            break;
        case '8': // DECRC -- Restore cursor
            m_cursorRow = std::clamp(m_savedRow, 0, m_rows - 1);
            m_cursorCol = std::clamp(m_savedCol, 0, m_cols - 1);
            m_dirty = true;
            m_state = State::Normal;
            break;
        case 'M': // RI -- Reverse Index (cursor up, scroll down if at top)
            if (m_cursorRow == m_scrollTop)
                ScrollDown(1);
            else if (m_cursorRow > 0)
                m_cursorRow--;
            m_dirty = true;
            m_state = State::Normal;
            break;
        case 'D': // IND -- Index (cursor down, scroll up if at bottom)
            LineFeed();
            m_state = State::Normal;
            break;
        case 'E': // NEL -- Next Line
            m_cursorCol = 0;
            LineFeed();
            m_state = State::Normal;
            break;
        case '=':
        case '>': // Keypad modes -- ignore
            m_state = State::Normal;
            break;
        case 'c': // RIS -- Full reset
            Reset();
            m_state = State::Normal;
            break;
        default:
            // Unknown ESC sequence -- ignore
            m_state = State::Normal;
            break;
    }
}

// ============================================================================
// CSI sequence processing
// ============================================================================

void Vt100Terminal::ProcessCsi(char ch)
{
    if (ch == '?')
    {
        m_csiQuestion = true;
        return;
    }

    if (ch >= '0' && ch <= '9')
    {
        m_currentParam = m_currentParam * 10 + (ch - '0');
        m_hasCurrentParam = true;
        return;
    }

    if (ch == ';')
    {
        m_csiParams.push_back(m_hasCurrentParam ? m_currentParam : 0);
        m_currentParam = 0;
        m_hasCurrentParam = false;
        return;
    }

    // Final character -- execute the CSI command
    if (m_hasCurrentParam)
        m_csiParams.push_back(m_currentParam);

    ExecuteCsi(ch);
    m_state = State::Normal;
}

int Vt100Terminal::Param(int index, int defaultValue) const
{
    if (index < static_cast<int>(m_csiParams.size()) && m_csiParams[index] > 0)
        return m_csiParams[index];
    return defaultValue;
}

void Vt100Terminal::ExecuteCsi(char cmd)
{
    if (m_csiQuestion)
    {
        // DEC private modes
        int mode = Param(0);
        if (cmd == 'h') // Set mode
        {
            if (mode == 1) m_applicationCursorKeys = true; // DECCKM
        }
        else if (cmd == 'l') // Reset mode
        {
            if (mode == 1) m_applicationCursorKeys = false; // DECCKM
        }
        // CSI ? 25 h/l = show/hide cursor (ignore)
        // CSI ? 1049 h/l = alternate screen buffer (ignore)
        return;
    }

    switch (cmd)
    {
        case 'A': // CUU -- Cursor Up
            m_cursorRow = std::max(m_cursorRow - Param(0), 0);
            m_dirty = true;
            break;

        case 'B': // CUD -- Cursor Down
            m_cursorRow = std::min(m_cursorRow + Param(0), m_rows - 1);
            m_dirty = true;
            break;

        case 'C': // CUF -- Cursor Forward (Right)
            m_cursorCol = std::min(m_cursorCol + Param(0), m_cols - 1);
            m_dirty = true;
            break;

        case 'D': // CUB -- Cursor Backward (Left)
            m_cursorCol = std::max(m_cursorCol - Param(0), 0);
            m_dirty = true;
            break;

        case 'H': // CUP -- Cursor Position (1-based)
        case 'f':
            m_cursorRow = std::clamp(Param(0) - 1, 0, m_rows - 1);
            m_cursorCol = std::clamp(Param(1, 1) - 1, 0, m_cols - 1);
            m_dirty = true;
            break;

        case 'G': // CHA -- Cursor Horizontal Absolute (1-based)
            m_cursorCol = std::clamp(Param(0) - 1, 0, m_cols - 1);
            m_dirty = true;
            break;

        case 'd': // VPA -- Cursor Vertical Absolute (1-based)
            m_cursorRow = std::clamp(Param(0) - 1, 0, m_rows - 1);
            m_dirty = true;
            break;

        case 'J': // ED -- Erase in Display
            EraseInDisplay(Param(0, 0));
            break;

        case 'K': // EL -- Erase in Line
            EraseInLine(Param(0, 0));
            break;

        case 'L': // IL -- Insert Lines
            InsertLines(Param(0));
            break;

        case 'M': // DL -- Delete Lines
            DeleteLines(Param(0));
            break;

        case 'P': // DCH -- Delete Characters
            DeleteChars(Param(0));
            break;

        case '@': // ICH -- Insert Blank Characters
            InsertChars(Param(0));
            break;

        case 'X': // ECH -- Erase Characters
            EraseChars(Param(0));
            break;

        case 'r': // DECSTBM -- Set Scrolling Region (1-based)
            m_scrollTop = std::clamp(Param(0) - 1, 0, m_rows - 1);
            m_scrollBottom = std::clamp(Param(1, m_rows) - 1, 0, m_rows - 1);
            if (m_scrollTop > m_scrollBottom)
                std::swap(m_scrollTop, m_scrollBottom);
            m_cursorRow = 0;
            m_cursorCol = 0;
            m_dirty = true;
            break;

        case 'S': // SU -- Scroll Up
            ScrollUp(Param(0));
            break;

        case 'T': // SD -- Scroll Down
            ScrollDown(Param(0));
            break;

        case 'm': // SGR -- Select Graphic Rendition (colors/bold -- ignore for now)
            break;

        case 'h':
        case 'l': // SM/RM -- Set/Reset Mode (ignore)
            break;

        case 'n': // DSR -- Device Status Report (ignore)
            break;

        case 's': // SCP -- Save Cursor Position
            m_savedRow = m_cursorRow;
            m_savedCol = m_cursorCol;
            break;

        case 'u': // RCP -- Restore Cursor Position
            m_cursorRow = std::clamp(m_savedRow, 0, m_rows - 1);
            m_cursorCol = std::clamp(m_savedCol, 0, m_cols - 1);
            m_dirty = true;
            break;
    }
}

// ============================================================================
// Erase operations
// ============================================================================

void Vt100Terminal::EraseInDisplay(int mode)
{
    switch (mode)
    {
        case 0: // Erase from cursor to end of screen
            ClearRange(m_cursorRow, m_cursorCol, m_rows - 1, m_cols - 1);
            break;
        case 1: // Erase from start of screen to cursor
            ClearRange(0, 0, m_cursorRow, m_cursorCol);
            break;
        case 2: // Erase entire screen
            ClearScreen();
            break;
    }
    m_dirty = true;
}

void Vt100Terminal::EraseInLine(int mode)
{
    switch (mode)
    {
        case 0: // Erase from cursor to end of line
            for (int c = m_cursorCol; c < m_cols; c++)
                ScreenAt(m_cursorRow, c) = ' ';
            break;
        case 1: // Erase from start of line to cursor
            for (int c = 0; c <= m_cursorCol && c < m_cols; c++)
                ScreenAt(m_cursorRow, c) = ' ';
            break;
        case 2: // Erase entire line
            for (int c = 0; c < m_cols; c++)
                ScreenAt(m_cursorRow, c) = ' ';
            break;
    }
    m_dirty = true;
}

void Vt100Terminal::EraseChars(int n)
{
    for (int i = 0; i < n && m_cursorCol + i < m_cols; i++)
        ScreenAt(m_cursorRow, m_cursorCol + i) = ' ';
    m_dirty = true;
}

// ============================================================================
// Insert/Delete operations
// ============================================================================

void Vt100Terminal::InsertLines(int n)
{
    int bottom = m_scrollBottom;
    for (int i = 0; i < n; i++)
    {
        // Shift lines down within scroll region
        for (int r = bottom; r > m_cursorRow; r--)
            for (int c = 0; c < m_cols; c++)
                ScreenAt(r, c) = ScreenAt(r - 1, c);
        // Clear the inserted line
        for (int c = 0; c < m_cols; c++)
            ScreenAt(m_cursorRow, c) = ' ';
    }
    m_dirty = true;
}

void Vt100Terminal::DeleteLines(int n)
{
    int bottom = m_scrollBottom;
    for (int i = 0; i < n; i++)
    {
        // Shift lines up within scroll region
        for (int r = m_cursorRow; r < bottom; r++)
            for (int c = 0; c < m_cols; c++)
                ScreenAt(r, c) = ScreenAt(r + 1, c);
        // Clear the bottom line
        for (int c = 0; c < m_cols; c++)
            ScreenAt(bottom, c) = ' ';
    }
    m_dirty = true;
}

void Vt100Terminal::DeleteChars(int n)
{
    for (int i = m_cursorCol; i < m_cols; i++)
    {
        int src = i + n;
        ScreenAt(m_cursorRow, i) = src < m_cols ? ScreenAt(m_cursorRow, src) : ' ';
    }
    m_dirty = true;
}

void Vt100Terminal::InsertChars(int n)
{
    for (int i = m_cols - 1; i >= m_cursorCol + n; i--)
        ScreenAt(m_cursorRow, i) = ScreenAt(m_cursorRow, i - n);
    for (int i = 0; i < n && m_cursorCol + i < m_cols; i++)
        ScreenAt(m_cursorRow, m_cursorCol + i) = ' ';
    m_dirty = true;
}

// ============================================================================
// Scrolling
// ============================================================================

void Vt100Terminal::ScrollUp(int n)
{
    for (int i = 0; i < n; i++)
    {
        // Save the top line to scrollback ring buffer before it's overwritten
        if (m_scrollTop == 0 && m_maxScrollback > 0)
        {
            std::string line(m_cols, ' ');
            for (int c = 0; c < m_cols; c++)
                line[c] = ScreenAt(m_scrollTop, c);

            // Trim trailing spaces
            auto end = line.find_last_not_of(' ');
            if (end != std::string::npos)
                line.erase(end + 1);
            else
                line.clear();

            int writeIdx = (m_scrollbackHead + m_scrollbackCount) % m_maxScrollback;
            m_scrollback[writeIdx] = std::move(line);
            if (m_scrollbackCount < m_maxScrollback)
                m_scrollbackCount++;
            else
                m_scrollbackHead = (m_scrollbackHead + 1) % m_maxScrollback; // Overwrite oldest
        }

        for (int r = m_scrollTop; r < m_scrollBottom; r++)
            for (int c = 0; c < m_cols; c++)
                ScreenAt(r, c) = ScreenAt(r + 1, c);
        for (int c = 0; c < m_cols; c++)
            ScreenAt(m_scrollBottom, c) = ' ';
    }
    m_dirty = true;
}

void Vt100Terminal::ScrollDown(int n)
{
    for (int i = 0; i < n; i++)
    {
        for (int r = m_scrollBottom; r > m_scrollTop; r--)
            for (int c = 0; c < m_cols; c++)
                ScreenAt(r, c) = ScreenAt(r - 1, c);
        for (int c = 0; c < m_cols; c++)
            ScreenAt(m_scrollTop, c) = ' ';
    }
    m_dirty = true;
}

// ============================================================================
// Helpers
// ============================================================================

void Vt100Terminal::ClearScreen()
{
    std::fill(m_screen.begin(), m_screen.end(), ' ');
    m_dirty = true;
}

void Vt100Terminal::ClearRange(int r1, int c1, int r2, int c2)
{
    for (int r = r1; r <= r2 && r < m_rows; r++)
    {
        int startC = (r == r1) ? c1 : 0;
        int endC = (r == r2) ? c2 : m_cols - 1;
        for (int c = startC; c <= endC && c < m_cols; c++)
            ScreenAt(r, c) = ' ';
    }
}

void Vt100Terminal::Reset()
{
    ClearScreen();
    m_cursorRow = 0;
    m_cursorCol = 0;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_alternateCharset = false;
    m_state = State::Normal;
    m_dirty = true;
}

} // namespace Em68030::Views
