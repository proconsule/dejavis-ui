std::string getISO8601Timestamp(const fs::path& p) {
    try {
        auto ftime = fs::last_write_time(p);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        std::time_t ctime = std::chrono::system_clock::to_time_t(sctp);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&ctime));
        return std::string(buf);
    } catch (...) {
        return "2026-01-01T00:00:00Z"; // Fallback
    }
}