#include "AppHdr.h"

#include "format.h"

#include <climits>

#include "colour.h"
#include "lang-fake.h"
#include "libutil.h"
#include "stringutil.h"
#include "unicode.h"
#include "viewchar.h"

formatted_string::formatted_string(int init_colour)
    : ops()
{
    if (init_colour)
        textcolour(init_colour);
}

formatted_string::formatted_string(const string &s, int init_colour)
    : ops()
{
    if (init_colour)
        textcolour(init_colour);
    cprintf(s);
}

/**
 * For a given tag, return the corresponding colour.
 *
 * @param tag       The tag: e.g. "red", "lightblue", "h", "w".
 * @return          The corresponding colour (e.g. RED), or LIGHTGREY.
 */
int formatted_string::get_colour(const string &tag)
{
    if (tag == "h")
        return YELLOW;

    if (tag == "w")
        return WHITE;

    return str_to_colour(tag);
}

// Display a formatted string without printing literal \n.
// This is important if the text is not supposed
// to clobber existing text to the right of the lines being displayed
// (some of the tutorial messages need this).
void display_tagged_block(const string &s)
{
    vector<formatted_string> lines;
    formatted_string::parse_string_to_multiple(s, lines);

    int x = wherex();
    int y = wherey();
    const unsigned int max_y = cgetsize(GOTO_CRT).y;
    const int size = min<unsigned int>(lines.size(), max_y - y + 1);
    for (int i = 0; i < size; ++i)
    {
        cgotoxy(x, y);
        lines[i].display();
        y++;
    }
}

/**
 * Take a string and turn it into a formatted_string.
 *
 * @param s             The input string: e.g. "<red>foo</red>".
 * @param main_colour   The initial & default text colour.
 * @return          A formatted string corresponding to the input.
 */
formatted_string formatted_string::parse_string(const string &s,
                                                int main_colour)
{
    // main_colour will usually be LIGHTGREY (default).
    vector<int> colour_stack(1, main_colour);

    formatted_string fs;

    parse_string1(s, fs, colour_stack);

    while (colour_stack.size() > 1)
        fs.pop_colour(colour_stack);

    return fs;
}

// Parses a formatted string in much the same way as parse_string, but
// handles \n by creating a new formatted_string.
void formatted_string::parse_string_to_multiple(const string &s,
                                                vector<formatted_string> &out,
                                                int wrap_col)
{
    vector<string> lines = split_string("\n", s, false, true);
    if (wrap_col > 0)
    {
        vector<string> pre_split = move(lines);
        for (string &line : pre_split)
        {
            do
            {
                lines.push_back(wordwrap_line(line, wrap_col, true, true));
            }
            while (!line.empty());
        }
    }

    vector<int> colour_stack(1, LIGHTGREY);

    for (const string &line : lines)
    {
        out.emplace_back();
        formatted_string& fs = out.back();

        for (size_t i = 1; i < colour_stack.size(); ++i)
            fs.ops.emplace_back(colour_stack[i]);

        parse_string1(line, fs, colour_stack);

        for (size_t i = colour_stack.size() - 1; i > 0; --i)
        {
            fs.ops.emplace_back(colour_stack[i-1]);
            fs.ops.back().closing_colour = colour_stack[i];
        }
    }
}

// Helper for the other parse_ methods.
void formatted_string::parse_string1(const string &s, formatted_string &fs,
                                     vector<int> &colour_stack)
{
    // FIXME: This is a lame mess, just good enough for the task on hand
    // (keyboard help).
    string::size_type tag    = string::npos;
    string::size_type length = s.length();

    string currs;

    for (tag = 0; tag < length; ++tag)
    {
        bool revert_colour = false;
        string::size_type endpos = string::npos;

        // Break string up if it gets too big.
        if (currs.size() >= 999)
        {
            // Break the string at the end of a line, if possible, so
            // that none of the broken string ends up overwritten.
            string::size_type bound = currs.rfind("\n", 999);
            if (bound != endpos)
                bound++;
            else
                bound = 999;

            fs.cprintf(currs.substr(0, bound));
            if (currs.size() > bound)
                currs = currs.substr(bound);
            else
                currs.clear();
            tag--;
            continue;
        }

        if (s[tag] != '<' || tag >= length - 1)
        {
            currs += s[tag];
            continue;
        }

        // Is this a << escape?
        if (s[tag + 1] == '<')
        {
            currs += s[tag];
            tag++;
            continue;
        }

        endpos = s.find('>', tag + 1);
        // No closing >?
        if (endpos == string::npos)
        {
            currs += s[tag];
            continue;
        }

        string tagtext = s.substr(tag + 1, endpos - tag - 1);
        if (tagtext.empty() || tagtext == "/")
        {
            currs += s[tag];
            continue;
        }

        if (tagtext[0] == '/')
        {
            revert_colour = true;
            tagtext = tagtext.substr(1);
            tag++;
        }

        if (!currs.empty())
        {
            fs.cprintf(currs);
            currs.clear();
        }

        if (revert_colour)
        {
            const int endcolour = get_colour(tagtext);

            if (colour_stack.size() > 1 && endcolour == colour_stack.back() && endcolour != -1)
                fs.pop_colour(colour_stack);
            else
            {
                fs.push_colour(LIGHTRED, colour_stack);
                fs.cprintf("</%s>", tagtext.c_str());
                fs.pop_colour(colour_stack);
            }
        }
        else
        {
            const int colour = get_colour(tagtext);

            if (colour == -1)
            {
                fs.push_colour(LIGHTRED, colour_stack);
                fs.cprintf("<%s>", tagtext.c_str());
                fs.pop_colour(colour_stack);
            }
            else
                fs.push_colour(colour, colour_stack);
        }

        tag += tagtext.length() + 1;
    }
    if (currs.length())
        fs.cprintf(currs);
}

/// Return a plaintext version of this string, sans tags, colours, etc.
formatted_string::operator string() const
{
    string s;
    for (const fs_op &op : ops)
        if (op.type == FSOP_TEXT)
            s += op.text;

    return s;
}

static void _replace_all_in_string(string& s, const string& search,
                                   const string& replace)
{
    string::size_type pos = 0;
    while ((pos = s.find(search, pos)) != string::npos)
    {
        s.replace(pos, search.size(), replace);
        pos += replace.size();
    }
}

string formatted_string::html_dump() const
{
    string s;
    for (const fs_op &op : ops)
    {
        string tmp;
        switch (op.type)
        {
        case FSOP_TEXT:
            tmp = op.text;
            // (very) crude HTMLification
            _replace_all_in_string(tmp, "&", "&amp;");
            _replace_all_in_string(tmp, " ", "&nbsp;");
            _replace_all_in_string(tmp, "<", "&lt;");
            _replace_all_in_string(tmp, ">", "&gt;");
            _replace_all_in_string(tmp, "\n", "<br>");
            s += tmp;
            break;
        case FSOP_COLOUR:
            s += "<font color=";
            s += colour_to_str(op.colour);
            s += ">";
            break;
        }
    }
    return s;
}

bool formatted_string::operator < (const formatted_string &other) const
{
    return string(*this) < string(other);
}

bool formatted_string::operator == (const formatted_string &other) const
{
    // May produce false negative in some cases, e.g. duplicated colour ops
    return ops == other.ops;
}

const formatted_string &
formatted_string::operator += (const formatted_string &other)
{
    ops.insert(ops.end(), other.ops.begin(), other.ops.end());
    return *this;
}

const formatted_string &
formatted_string::operator += (const string& other)
{
    ops.emplace_back(other);
    return *this;
}

int formatted_string::width() const
{
    // Just add up the individual string lengths.
    int len = 0;
    for (const fs_op &op : ops)
        if (op.type == FSOP_TEXT)
            len += strwidth(op.text);
    return len;
}

static inline void cap(int &i, int max)
{
    if (i < 0 && -i <= max)
        i += max;
    if (i >= max)
        i = max - 1;
    if (i < 0)
        i = 0;
}

char &formatted_string::operator [] (size_t idx)
{
    size_t rel_idx = idx;
    int size = ops.size();
    for (int i = 0; i < size; ++i)
    {
        if (ops[i].type != FSOP_TEXT)
            continue;

        size_t len = ops[i].text.length();
        if (rel_idx >= len)
            rel_idx -= len;
        else
            return ops[i].text[rel_idx];
    }
    die("Invalid index");
}

string formatted_string::tostring(int s, int e) const
{
    string st;

    int size = ops.size();
    cap(s, size);
    cap(e, size);

    for (int i = s; i <= e && i < size; ++i)
    {
        if (ops[i].type == FSOP_TEXT)
            st += ops[i].text;
    }
    return st;
}

string formatted_string::to_colour_string() const
{
    string st;
    const int size = ops.size();
    for (int i = 0; i < size; ++i)
    {
        if (ops[i].type == FSOP_TEXT)
        {
            // gotta double up those '<' chars ...
            size_t start = st.size();
            st += ops[i].text;

            while (true)
            {
                const size_t left_angle = st.find('<', start);
                if (left_angle == string::npos)
                    break;

                st.insert(left_angle, "<");
                start = left_angle + 2;
            }
        }
        else if (ops[i].type == FSOP_COLOUR)
        {
            st += "<";
            if (ops[i].closing_colour == static_cast<colour_t>(-1))
                st += colour_to_str(ops[i].colour);
            else
                st += "/" + colour_to_str(ops[i].closing_colour);
            st += ">";
        }
    }

    return st;
}

void formatted_string::display(int s, int e) const
{
    int size = ops.size();
    if (!size)
        return;

    cap(s, size);
    cap(e, size);

    for (int i = s; i <= e && i < size; ++i)
        ops[i].display();
}

int formatted_string::find_last_colour() const
{
    if (!ops.empty())
    {
        for (int i = ops.size() - 1; i >= 0; --i)
            if (ops[i].type == FSOP_COLOUR)
                return ops[i].colour;
    }
    return LIGHTGREY;
}

formatted_string formatted_string::chop(int length) const
{
    formatted_string result;
    for (const fs_op& op : ops)
    {
        if (op.type == FSOP_TEXT)
        {
            result.ops.push_back(op);
            string& new_string = result.ops[result.ops.size()-1].text;
            int w = strwidth(new_string);
            if (w > length)
                new_string = chop_string(new_string, length, false);
            length -= w;
            if (length <= 0)
                break;
        }
        else
            result.ops.push_back(op);
    }

    return result;
}

formatted_string formatted_string::chop_bytes(int length) const
{
    return substr_bytes(0, length);
}

formatted_string formatted_string::substr_bytes(int pos, int length) const
{
    formatted_string result;
    fs_op initial(LIGHTGREY);
    for (const fs_op& op : ops)
    {
        if (op.type == FSOP_TEXT)
        {
            int n = op.text.size();
            if (pos >= n)
            {
                pos -= n;
                continue;
            }
            if (result.empty())
                result.ops.push_back(initial);
            result.ops.push_back(fs_op(op.text.substr(pos, length)));
            string& new_string = result.ops[result.ops.size()-1].text;
            pos = 0;
            length -= new_string.size();
            if (length <= 0)
                break;
        }
        else if (pos == 0)
            result.ops.push_back(op);
        else
            initial = op;
    }
    return result;
}

formatted_string formatted_string::trim() const
{
    return parse_string(trimmed_string(to_colour_string()));
}

void formatted_string::del_char()
{
    for (auto i = ops.begin(); i != ops.end(); ++i)
    {
        if (i->type != FSOP_TEXT)
            continue;
        switch (strwidth(i->text))
        {
        case 0: // shouldn't happen
            continue;
        case 1:
            ops.erase(i);
            return;
        }
        i->text = next_glyph((char*)i->text.c_str());
        return;
    }
}

void formatted_string::add_glyph(cglyph_t g)
{
    const int last_col = find_last_colour();
    if (last_col != g.col)
        textcolour(g.col);
    cprintf("%s", stringize_glyph(g.ch).c_str());
    if (last_col != g.col)
    {
        textcolour(last_col);
        ops.back().closing_colour = g.col;
    }
}

void formatted_string::textcolour(int colour)
{
    ops.emplace_back(colour);
}

void formatted_string::push_colour(int colour, vector<int> &colour_stack)
{
    ops.emplace_back(colour);
    colour_stack.emplace_back(colour);
}

void formatted_string::pop_colour(vector<int> &colour_stack)
{
    const auto previous = colour_stack.back();
    colour_stack.pop_back();
    const auto current = colour_stack.back();
    ops.emplace_back(current);
    ops.back().closing_colour = previous;
}

void formatted_string::clear()
{
    ops.clear();
}

bool formatted_string::empty() const
{
    return ops.empty();
}

void formatted_string::cprintf(const char *s, ...)
{
    va_list args;
    va_start(args, s);
    cprintf(vmake_stringf(s, args));
    va_end(args);
}

void formatted_string::cprintf(const string &s)
{
    ops.push_back(s);
}

void formatted_string::fs_op::display() const
{
    switch (type)
    {
    case FSOP_COLOUR:
#ifndef USE_TILE_LOCAL
        if (colour < NUM_TERM_COLOURS)
#endif
            ::textcolour(colour);
        break;
    case FSOP_TEXT:
        ::cprintf("%s", text.c_str());
        break;
    }
}

void formatted_string::swap(formatted_string& other)
{
    ops.swap(other.ops);
}

void formatted_string::all_caps()
{
    for (fs_op &op : ops)
        if (op.type == FSOP_TEXT)
            uppercase(op.text);
}

void formatted_string::capitalise()
{
    for (fs_op &op : ops)
        if (op.type == FSOP_TEXT && !op.text.empty())
        {
            op.text = uppercase_first(op.text);
            break;
        }
}

void formatted_string::filter_lang()
{
    for (fs_op &op : ops)
        if (op.type == FSOP_TEXT)
            ::filter_lang(op.text);
}

int count_linebreaks(const formatted_string& fs)
{
    string::size_type where = 0;
    const string s = fs;
    int count = 0;
    while (1)
    {
        where = s.find("\n", where);
        if (where == string::npos)
            break;
        else
        {
            ++count;
            ++where;
        }
    }
    return count;
}

int _find_index_before_width(const char *text, int max_str_width)
{
    int width = 0;

    for (char *itr = (char *)text; *itr; itr = next_glyph(itr))
    {
        if (*itr == '\n')
            return INT_MAX;

        char32_t ch;
        utf8towc(&ch, itr);

        int cw = wcwidth(ch);
        if (cw != -1) // shouldn't ever happen
            width += cw;
        if (width > max_str_width)
            return itr-text;
    }

    return INT_MAX;
}

static int _find_newline(const char *s)
{
    const char *nl = strchr(s, '\n');
    return nl ? nl-s : INT_MAX;
}

/**
 * Linebreaks a formatted string into the specified width and height.
 *
 * @param str            The input string: e.g. "<red>foo</red>".
 * @param max_str_width  The maximum width of the output string.
 * @param max_str_height The maximum height of the output string.
 * @return          The wrapped formatted string.
 */
formatted_string linebreak_formatted_string(const formatted_string &str, int max_str_width, int max_str_height)
{
    const int max_lines = max_str_height;

    if (max_lines < 1)
        return formatted_string();

    formatted_string ret;
    ret += str;

    string base = str.tostring();
    int num_lines = 0;

    char *line = &base[0];
    while (true)
    {
        int nl = _find_newline(line);
        int line_end = _find_index_before_width(line, max_str_width);
        if (line_end == INT_MAX && nl == INT_MAX)
            break;

        int space_idx = 0;
        if (nl < line_end)
            space_idx = nl;
        else
        {
            space_idx = -1;
            for (char *search = &line[line_end];
                 search > line;
                 search = prev_glyph(search, line))
            {
                if (*search == ' ')
                {
                    space_idx = search - line;
                    break;
                }
            }
        }

        if (++num_lines >= max_lines || space_idx == -1)
        {
            line_end = min(line_end, nl);
            int ellipses;
            if (space_idx != -1 && space_idx - line_end > 2)
                ellipses = space_idx;
            else
            {
                ellipses = line_end;
                for (unsigned i = 0; i < strlen(".."); i++)
                {
                    char *prev = prev_glyph(&line[ellipses], line);
                    ellipses = (prev ? prev : line) - line;
                }
            }

            ret = ret.chop_bytes(&line[ellipses] - &base[0]);
            ret += "..";
            return ret;
        }
        else if (space_idx != nl)
        {
            line[space_idx] = '\n';
            ret[&line[space_idx] - &base[0]] = '\n';
        }

        line = &line[space_idx+1];
    }

    return ret;
}

/**
 * Determines the maximum width of a (possibly multiline) formatted_string.
 *
 * In contrast to width(), which just detmines the total 'length'.
 *
 * @return The maximum width, in characters.
 */
int formatted_string::string_width() const
{
    unsigned int max_str_width = 0;
    unsigned int width = 0;
    const auto s = tostring();

    for (char *itr = (char *)s.c_str(); *itr; itr = next_glyph(itr))
    {
        if (*itr == '\n')
        {
            max_str_width = max(width, max_str_width);
            width = 0;
        }
        else
        {
            char32_t ch;
            utf8towc(&ch, itr);
            width += wcwidth(ch);
        }
    }

    return max(width, max_str_width);

}

/**
 * Determines the maximum number of lines in a formatted_string.
 *
 * @return The maximum height, in lines.
 */
int formatted_string::string_height() const
{
    const auto& s = tostring();
    return count(begin(s), end(s), '\n') + 1;
}
