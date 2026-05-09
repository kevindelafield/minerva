#pragma once

#include <iosfwd>
#include <memory>
#include <string>

namespace minerva
{
    /**
     * Atomic-overwrite output file.
     *
     * Writes go to a sibling temp file; commit() fsync's the data, atomically
     * rename(2)'s into place, and fsync's the parent directory so the new
     * contents survive a crash. If commit() is never called (or fails), the
     * destructor unlinks the temp file.
     *
     * Usage:
     *     safe_ofstream os(path);
     *     if (!os.is_open()) { ... }
     *     os << "data" << std::endl;
     *     if (!os.commit()) { ... }
     */
    class safe_ofstream final
    {
    public:
        explicit safe_ofstream(const std::string& filename);
        ~safe_ofstream();

        safe_ofstream(const safe_ofstream&)            = delete;
        safe_ofstream& operator=(const safe_ofstream&) = delete;
        safe_ofstream(safe_ofstream&&) noexcept;
        safe_ofstream& operator=(safe_ofstream&&) noexcept;

        bool is_open()      const { return m_open; }
        bool is_committed() const { return m_committed; }
        bool fail()         const;
        bool bad()          const;

        /**
         * Atomically replace the target with the staged contents. fsync's the
         * data before rename and the parent directory after. Returns false on
         * any I/O failure (the temp file is then removed by the destructor).
         */
        bool commit();

        const std::string& get_temp_path()   const { return m_fakepath; }
        const std::string& get_target_path() const { return m_realpath; }

        // Stream-style writing. Forwards to the underlying ostream.
        template<typename T>
        safe_ofstream& operator<<(const T& v)
        {
            stream() << v;
            return *this;
        }
        // Manipulators (std::endl, std::flush, ...).
        safe_ofstream& operator<<(std::ostream& (*manip)(std::ostream&))
        {
            stream() << manip;
            return *this;
        }

        std::ostream& stream();

    private:
        void cleanup_temp() noexcept;

        struct impl;
        std::unique_ptr<impl> m_impl;

        std::string m_fakepath;
        std::string m_realpath;
        int  m_fd        = -1;
        bool m_open      = false;
        bool m_committed = false;
    };
}
