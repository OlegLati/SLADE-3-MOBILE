#pragma once
// -----------------------------------------------------------------------------
// android_compat.h
//
// Minimal drop-in replacements for the small set of wxWidgets / SLADE-UI
// symbols that leak into otherwise UI-independent code (Archive/*,
// ArchiveManager, Utility/*, etc). Goal: let SLADE's format-parsing source
// compile unmodified (or nearly so) for a headless / Android build, without
// linking real wxWidgets.
//
// This is a STARTING POINT, not a finished shim. As you add more source
// files to slade_core, the compiler/linker will point out the next missing
// symbol -- add each one here as it comes up, rather than trying to predict
// all of them up front. Most of what's here are just enough of a type's
// interface to make headers PARSE; none of it needs to be functionally
// correct, since none of this compiled code should ever actually construct
// a real window, menu, or font at runtime in the headless build.
//
// Usage: force-included before any SLADE header via the build system
// (see CMakeLists.txt: target_compile_options(... -include android_compat.h)),
// so you should NOT #include this manually inside SLADE source files.
// -----------------------------------------------------------------------------

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// -----------------------------------------------------------------------
// 1. Endian swap macro
//    Real wx only swaps on big-endian targets; every current Android ABI
//    (arm64-v8a, armeabi-v7a, x86_64) is little-endian, so this is a no-op
//    in practice -- kept correct anyway in case that ever changes.
// -----------------------------------------------------------------------
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
inline uint32_t wxINT32_SWAP_ON_BE(uint32_t v)
{
    return __builtin_bswap32(v);
}
#else
inline uint32_t wxINT32_SWAP_ON_BE(uint32_t v)
{
    return v;
}
#endif

// -----------------------------------------------------------------------
// 2. Minimal wxString stand-in
//    Most of SLADE's codebase has moved to std::string/string_view, but a
//    handful of utility headers (General/CVar.h, Utility/StringUtils.h)
//    still reference the real wxString type directly. NOT a full
//    reimplementation -- just enough of the interface (construction from
//    a C string/std::string, FromUTF8, utf8_string, Cmp/CmpNoCase,
//    concatenation, equality) for those call sites to type-check.
// -----------------------------------------------------------------------
class wxString
{
public:
    wxString() = default;
    wxString(const char* s) : str_(s ? s : "") {}
    wxString(const std::string& s) : str_(s) {}
    wxString(std::string_view s) : str_(s) {}

    static wxString FromUTF8(std::string_view s) { return wxString(s); }
    static wxString FromUTF8(const char* s, size_t len) { return wxString(std::string(s, len)); }

    std::string utf8_string() const { return str_; }
    const char* c_str() const { return str_.c_str(); }

    bool   empty() const { return str_.empty(); }
    bool   IsEmpty() const { return str_.empty(); }
    size_t length() const { return str_.length(); }
    size_t size() const { return str_.size(); }

    int Cmp(const wxString& other) const { return str_.compare(other.str_); }
    int CmpNoCase(const wxString& other) const { return lower(str_).compare(lower(other.str_)); }

    wxString Lower() const { return wxString(lower(str_)); }
    wxString Upper() const { return wxString(upper(str_)); }

    // Real wx: Trim(bool fromRight = true) trims ONE side per call.
    wxString& Trim(bool fromRight = true)
    {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        if (fromRight)
        {
            while (!str_.empty() && isSpace(static_cast<unsigned char>(str_.back())))
                str_.pop_back();
        }
        else
        {
            size_t i = 0;
            while (i < str_.size() && isSpace(static_cast<unsigned char>(str_[i])))
                ++i;
            str_.erase(0, i);
        }
        return *this;
    }

    bool StartsWith(const wxString& prefix, wxString* rest = nullptr) const
    {
        if (str_.rfind(prefix.str_, 0) != 0)
            return false;
        if (rest)
            *rest = wxString(str_.substr(prefix.str_.size()));
        return true;
    }

    bool EndsWith(const wxString& suffix, wxString* rest = nullptr) const
    {
        if (suffix.str_.size() > str_.size())
            return false;
        if (str_.compare(str_.size() - suffix.str_.size(), suffix.str_.size(), suffix.str_) != 0)
            return false;
        if (rest)
            *rest = wxString(str_.substr(0, str_.size() - suffix.str_.size()));
        return true;
    }

    // Real wx: returns number of replacements made.
    size_t Replace(const wxString& from, const wxString& to, bool replaceAll = true)
    {
        if (from.str_.empty())
            return 0;
        size_t count = 0;
        size_t pos   = 0;
        while ((pos = str_.find(from.str_, pos)) != std::string::npos)
        {
            str_.replace(pos, from.str_.size(), to.str_);
            pos += to.str_.size();
            ++count;
            if (!replaceAll)
                break;
        }
        return count;
    }

    bool ToLong(long* val) const
    {
        try
        {
            size_t idx  = 0;
            long   v    = std::stol(str_, &idx);
            if (idx != str_.size())
                return false;
            if (val)
                *val = v;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ToULong(unsigned long* val) const
    {
        try
        {
            size_t        idx = 0;
            unsigned long v   = std::stoul(str_, &idx);
            if (idx != str_.size())
                return false;
            if (val)
                *val = v;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ToDouble(double* val) const
    {
        try
        {
            size_t idx = 0;
            double v   = std::stod(str_, &idx);
            if (idx != str_.size())
                return false;
            if (val)
                *val = v;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    wxString& Append(const wxString& other)
    {
        str_ += other.str_;
        return *this;
    }

    wxString& operator+=(const wxString& other)
    {
        str_ += other.str_;
        return *this;
    }
    wxString operator+(const wxString& other) const { return wxString(str_ + other.str_); }

    bool operator==(const wxString& other) const { return str_ == other.str_; }
    bool operator!=(const wxString& other) const { return str_ != other.str_; }

    operator std::string() const { return str_; }

private:
    static std::string lower(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    static std::string upper(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    std::string str_;
};

// Real wx wraps string literals for wide/UTF-8 builds; here it's just a
// pass-through, relying on wxString's implicit const-char* constructor.
#define wxS(x) (x)

// -----------------------------------------------------------------------
// 3. wxFile-compatible shim
//    Covers exactly the subset of the API SLADE's archive formats use:
//    Open (read/write), Read, Write, Seek, IsOpened, Close, plus the
//    wxFile::OpenMode / seek-mode constants referenced (wxFile::write,
//    wxFromStart, wxFromCurrent). Extend as needed.
// -----------------------------------------------------------------------
enum class WxCompatSeekMode
{
    FromStart,
    FromCurrent,
    FromEnd
};
constexpr WxCompatSeekMode wxFromStart   = WxCompatSeekMode::FromStart;
constexpr WxCompatSeekMode wxFromCurrent = WxCompatSeekMode::FromCurrent;
constexpr WxCompatSeekMode wxFromEnd     = WxCompatSeekMode::FromEnd;

class wxFile
{
public:
    enum OpenMode
    {
        read,
        write,
        read_write,
        write_append
    };

    wxFile() = default;
    explicit wxFile(const std::string& filename, OpenMode mode = read) { Open(filename, mode); }

    bool Open(const std::string& filename, OpenMode mode = read)
    {
        path_                      = filename;
        std::ios_base::openmode m = std::ios::binary;
        switch (mode)
        {
        case read: m |= std::ios::in; break;
        case write: m |= std::ios::out | std::ios::trunc; break;
        case read_write: m |= std::ios::in | std::ios::out; break;
        case write_append: m |= std::ios::out | std::ios::app; break;
        }
        stream_.open(filename, m);
        return stream_.is_open();
    }

    bool IsOpened() const { return stream_.is_open(); }

    bool Read(void* buffer, size_t count)
    {
        stream_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
        return !stream_.fail();
    }

    bool Write(const void* buffer, size_t count)
    {
        stream_.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));
        return !stream_.fail();
    }

    long Seek(long offset, WxCompatSeekMode mode = wxFromStart)
    {
        std::ios_base::seekdir dir = std::ios::beg;
        if (mode == WxCompatSeekMode::FromCurrent)
            dir = std::ios::cur;
        if (mode == WxCompatSeekMode::FromEnd)
            dir = std::ios::end;
        stream_.seekg(offset, dir);
        stream_.seekp(offset, dir);
        return static_cast<long>(stream_.tellg());
    }

    long Tell() { return static_cast<long>(stream_.tellg()); }

    long Length() const
    {
        std::error_code ec;
        auto             sz = std::filesystem::file_size(path_, ec);
        return ec ? -1 : static_cast<long>(sz);
    }

    void Close() { stream_.close(); }

    // Some SLADE code constructs the filename via wxString::FromUTF8(...).
    static std::string FromUTF8(const std::string& s) { return s; }

private:
    std::fstream stream_;
    std::string  path_;
};

// -----------------------------------------------------------------------
// 4. Splash / progress UI stubs
//    Archive parsing code calls these directly, mid-loop, to update a
//    splash-screen progress bar. In the headless/Android build there's
//    no such window, so these are no-ops for now.
//    TODO (later phase): wire these to a JNI callback so a real Android
//    ProgressBar can reflect load progress instead of just discarding it.
//    NOTE: it turns out SLADE's own General/UI.h, UI/WxUtils.h, and
//    General/Misc.h already DECLARE ui::showSplash/setSplashProgress*,
//    wxutil::strFromView, and misc::fileNameToLumpName/crc themselves --
//    we just hadn't hit those headers yet in earlier build attempts.
//    Declaring our own versions here collided with the real declarations
//    (wrong return type on strFromView, duplicate default argument on
//    fileNameToLumpName, etc). So: DON'T declare these here at all: only
//    global::error has no real declaration anywhere in SLADE's own
//    headers, so it's the only one that still needs to live in this
//    force-included header. Definitions for the rest now live in
//    compat/slade_shims.cpp, matching the real declared signatures.
// -----------------------------------------------------------------------
namespace slade
{
// -----------------------------------------------------------------------
// global::error -- a plain extern string the real code assigns error
// messages into (MemChunk.cpp etc: `global::error = "...";`). Declared as
// an inline variable so it has a single definition across every
// translation unit that includes this header (C++17 inline variables).
// -----------------------------------------------------------------------
namespace global
{
inline std::string error;
inline bool        debug = false;
}
} // namespace slade

// -----------------------------------------------------------------------
// 6. wxGetEnv replacement (used in ArchiveManager::init for $APPDIR)
//    Two overloads because different call sites pass either a std::string*
//    or a wxString* out-parameter.
// -----------------------------------------------------------------------
inline bool wxGetEnv(const char* name, std::string* value)
{
    const char* v = std::getenv(name);
    if (!v)
        return false;
    if (value)
        *value = v;
    return true;
}

inline bool wxGetEnv(const char* name, wxString* value)
{
    const char* v = std::getenv(name);
    if (!v)
        return false;
    if (value)
        *value = wxString(v);
    return true;
}

// -----------------------------------------------------------------------
// 7. wxColour -- a real (if minimal) value type, since SLADE's own
//    Utility/Colour.h constructs one via a braced 4-value initializer and
//    reads it back via Red()/Green()/Blue()/Alpha(). Used transitively by
//    Graphics/Palette code that Archive/EntryType headers pull in.
// -----------------------------------------------------------------------
class wxColour
{
public:
    wxColour() = default;
    wxColour(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
        : r_(r), g_(g), b_(b), a_(a)
    {
    }

    unsigned char Red() const { return r_; }
    unsigned char Green() const { return g_; }
    unsigned char Blue() const { return b_; }
    unsigned char Alpha() const { return a_; }

private:
    unsigned char r_ = 0, g_ = 0, b_ = 0, a_ = 255;
};

// -----------------------------------------------------------------------
// 8. Incomplete-type stand-ins for wx UI classes referenced only as
//    pointer/by-value types in FUNCTION DECLARATIONS (never defined or
//    called from headless code) -- General/UI.h, UI/WxUtils.h. A forward
//    declaration is enough for a declaration to parse; it would only need
//    to be complete if something actually called these functions or
//    dereferenced the pointers, which nothing in slade_core does.
// -----------------------------------------------------------------------
class wxWindow;
class wxMenu;
class wxMenuItem;
class wxFont;

// -----------------------------------------------------------------------
// 9. wxDirTraverser -- SLADE's DirArchiveTraverser (Archive/Formats/
//    DirArchive.h) derives from this and overrides both methods, so
//    (unlike section 8) this one needs real virtual methods to override,
//    not just a forward declaration.
// -----------------------------------------------------------------------
enum wxDirTraverseResult
{
    wxDIR_IGNORE,
    wxDIR_STOP,
    wxDIR_CONTINUE
};

class wxDirTraverser
{
public:
    virtual ~wxDirTraverser()                                    = default;
    virtual wxDirTraverseResult OnFile(const wxString& filename) = 0;
    virtual wxDirTraverseResult OnDir(const wxString& dirname)   = 0;
};

// -----------------------------------------------------------------------
// 10. More UI/WxUtils.h declaration-only types. Same rule as section 8:
//     forward declarations are enough for a function DECLARATION to parse;
//     nothing in slade_core calls these functions. wxSizerFlags is the one
//     exception -- it's used as a by-value default argument (`= {}`), so it
//     needs to be a complete, default-constructible type.
// -----------------------------------------------------------------------
class wxObject;
class wxImageList;
class wxPanel;
class wxSpinCtrl;
class wxSizer;
class wxButton;
class wxSize;
class wxPoint;
class wxRect;
class wxTopLevelWindow;
class wxImage;
class wxPalette;

// wxArrayString needs to be a real, complete type: FileUtils.cpp declares
// one by value and passes it by pointer to wxDir::GetAllFiles to be filled
// in. Thin wrapper over std::vector<wxString>.
class wxArrayString
{
public:
    wxArrayString() = default;

    void   Add(const wxString& s) { items_.push_back(s); }
    void   push_back(const wxString& s) { items_.push_back(s); }
    size_t GetCount() const { return items_.size(); }
    size_t size() const { return items_.size(); }
    bool   empty() const { return items_.empty(); }

    wxString&       operator[](size_t i) { return items_[i]; }
    const wxString& operator[](size_t i) const { return items_[i]; }

    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

private:
    std::vector<wxString> items_;
};

class wxSizerFlags
{
public:
    wxSizerFlags() = default;
};

// -----------------------------------------------------------------------
// 11. wxDateTime -- used as an actual class member (General/UndoRedo.h),
//     so (unlike section 10) needs to be a complete, default-constructible
//     type. Extend with real date/time storage if UndoRedo.cpp ever gets
//     added to the build and needs one.
// -----------------------------------------------------------------------
class wxDateTime
{
public:
    wxDateTime() = default;
};

// -----------------------------------------------------------------------
// 12. sf::Clock / sf::Time -- SFML is out of scope for now (see
//     compat/common2.h), but Archive.cpp uses sf::Clock directly to time
//     how long an archive took to load/save, so it needs to actually work,
//     not just compile. Implemented on top of std::chrono.
// -----------------------------------------------------------------------
namespace sf
{
class Time
{
public:
    Time() = default;
    explicit Time(std::chrono::microseconds us) : us_(us) {}

    float   asSeconds() const { return std::chrono::duration<float>(us_).count(); }
    int32_t asMilliseconds() const
    {
        return static_cast<int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(us_).count());
    }
    int64_t asMicroseconds() const { return us_.count(); }

private:
    std::chrono::microseconds us_{ 0 };
};

class Clock
{
public:
    Clock() : start_(std::chrono::steady_clock::now()) {}

    Time getElapsedTime() const
    {
        auto now = std::chrono::steady_clock::now();
        return Time(std::chrono::duration_cast<std::chrono::microseconds>(now - start_));
    }

    Time restart()
    {
        Time t = getElapsedTime();
        start_ = std::chrono::steady_clock::now();
        return t;
    }

private:
    std::chrono::steady_clock::time_point start_;
};
} // namespace sf

// -----------------------------------------------------------------------
// 13. wxFileName::FileExists / wxCopyFile -- used by Archive::save() for
//     the "back up existing file before overwriting" path. Implemented on
//     top of std::filesystem instead of stubbed, since this is real logic
//     that should actually work in the headless build too.
// -----------------------------------------------------------------------
class wxFileName
{
public:
    wxFileName() = default;
    explicit wxFileName(const wxString& fullpath) : path_(fullpath.utf8_string()) {}

    static bool FileExists(const wxString& path) { return std::filesystem::exists(path.utf8_string()); }

    wxString GetPath(bool add_separator = false) const
    {
        auto p = std::filesystem::path(path_).parent_path().string();
        if (add_separator && !p.empty() && p.back() != '/')
            p += '/';
        return wxString(p);
    }

    wxString GetFullPath() const { return wxString(path_); }

    static bool IsFileExecutable(const wxString& path)
    {
        struct stat st{};
        if (stat(path.utf8_string().c_str(), &st) != 0)
            return false;
        return (st.st_mode & S_IXUSR) != 0;
    }

private:
    std::string path_;
};

inline bool wxFileExists(const wxString& path)
{
    return std::filesystem::exists(path.utf8_string());
}

inline bool wxRemoveFile(const wxString& path)
{
    std::error_code ec;
    return std::filesystem::remove(path.utf8_string(), ec);
}

// -----------------------------------------------------------------------
// 14c. Directory operations -- wxDirExists, wxMkdir, wxDir (Remove /
//     GetAllFiles), wxFileModificationTime, and the flag constants
//     FileUtils.cpp uses them with. All implemented on std::filesystem.
// -----------------------------------------------------------------------
inline const wxString wxEmptyString{};

constexpr int wxDIR_FILES            = 1;
constexpr int wxDIR_DIRS             = 2;
constexpr int wxPATH_RMDIR_RECURSIVE = 1;

inline bool wxDirExists(const wxString& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path.utf8_string(), ec);
}

inline bool wxMkdir(const wxString& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.utf8_string(), ec);
    return !ec;
}

class wxDir
{
public:
    static bool Remove(const wxString& path, int flags = 0)
    {
        std::error_code ec;
        if (flags & wxPATH_RMDIR_RECURSIVE)
            std::filesystem::remove_all(path.utf8_string(), ec);
        else
            std::filesystem::remove(path.utf8_string(), ec);
        return !ec;
    }

    static bool GetAllFiles(
        const wxString& dir,
        wxArrayString*  files,
        const wxString& filespec = wxEmptyString,
        int              flags   = wxDIR_FILES)
    {
        (void)filespec; // wildcard filtering not implemented -- not used by current call sites
        if (!files)
            return false;
        std::error_code ec;
        if (!std::filesystem::exists(dir.utf8_string(), ec))
            return false;

        for (auto& entry : std::filesystem::recursive_directory_iterator(
                 dir.utf8_string(), std::filesystem::directory_options::skip_permission_denied, ec))
        {
            bool isDir = entry.is_directory(ec);
            if (isDir && !(flags & wxDIR_DIRS))
                continue;
            if (!isDir && !(flags & wxDIR_FILES))
                continue;
            files->push_back(wxString(entry.path().string()));
        }
        return true;
    }
};

inline long wxFileModificationTime(const wxString& path)
{
    std::error_code ec;
    struct stat     st{};
    if (stat(path.utf8_string().c_str(), &st) != 0)
        return 0;
    return static_cast<long>(st.st_mtime);
}

// -----------------------------------------------------------------------
// 14b. wxTextFile -- minimal line-based text file reader, used by
//     StringUtils.cpp's #include-processing helpers. Supports both the
//     indexed (GetLineCount/GetLine) and cursor (GetFirstLine/GetNextLine)
//     styles of the real API, since we can't see which one call sites use
//     without the full source.
// -----------------------------------------------------------------------
class wxTextFile
{
public:
    wxTextFile() = default;
    explicit wxTextFile(const wxString& filename) { Open(filename); }

    bool Open(const wxString& filename)
    {
        std::ifstream in(filename.utf8_string());
        if (!in.is_open())
            return false;
        lines_.clear();
        std::string line;
        while (std::getline(in, line))
            lines_.emplace_back(line);
        idx_ = 0;
        return true;
    }

    size_t          GetLineCount() const { return lines_.size(); }
    wxString&       GetLine(size_t n) { return lines_[n]; }
    const wxString& GetLine(size_t n) const { return lines_[n]; }

    wxString& GetFirstLine()
    {
        idx_ = 0;
        return lines_.empty() ? empty_ : lines_[0];
    }
    wxString& GetNextLine() { return (++idx_ < lines_.size()) ? lines_[idx_] : empty_; }
    bool      Eof() const { return idx_ + 1 >= lines_.size(); }

    void AddLine(const wxString& line) { lines_.push_back(line); }
    void Clear() { lines_.clear(); }
    void Close() { lines_.clear(); }

private:
    std::vector<wxString> lines_;
    size_t                 idx_ = 0;
    wxString                empty_;
};

// Real wx: #define wxCHECK_VERSION(major,minor,release) (wx is at least
// that version). We have no "real" wx version, so this just picks the
// branch that matches the wxString API we've actually implemented above
// (the 2-argument FromUTF8(data, len) overload).
#define wxCHECK_VERSION(major, minor, release) 1

inline bool wxCopyFile(const wxString& from, const wxString& to, bool overwrite = true)
{
    std::error_code ec;
    auto             opts = overwrite ? std::filesystem::copy_options::overwrite_existing
                                       : std::filesystem::copy_options::none;
    return std::filesystem::copy_file(from.utf8_string(), to.utf8_string(), opts, ec);
}

// -----------------------------------------------------------------------
// 14. wxRegEx -- StringUtils.cpp builds several file-scope regex objects
//     and actually matches against them (integer/float detection), so this
//     needs to be a real, working implementation, not a stub. The patterns
//     used are simple ERE-style (^, $, [...], +, ?) which std::regex's
//     default ECMAScript grammar handles fine.
// -----------------------------------------------------------------------
#include <regex>

constexpr int wxRE_DEFAULT = 0;
constexpr int wxRE_NOSUB   = 1;
constexpr int wxRE_ICASE   = 2;

class wxRegEx
{
public:
    wxRegEx() = default;
    explicit wxRegEx(const wxString& pattern, int flags = wxRE_DEFAULT) { Compile(pattern, flags); }

    bool Compile(const wxString& pattern, int flags = wxRE_DEFAULT)
    {
        try
        {
            auto opts = std::regex::ECMAScript;
            if (flags & wxRE_ICASE)
                opts |= std::regex::icase;
            regex_ = std::regex(pattern.utf8_string(), opts);
            valid_ = true;
        }
        catch (const std::regex_error&)
        {
            valid_ = false;
        }
        return valid_;
    }

    bool IsValid() const { return valid_; }

    bool Matches(const wxString& text) const
    {
        return valid_ && std::regex_search(text.utf8_string(), regex_);
    }

private:
    std::regex regex_;
    bool       valid_ = false;
};
