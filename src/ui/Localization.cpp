#include "Localization.hpp"

#include <toml++/toml.h>
#include <plog/Log.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace i18n
{
    namespace {
        std::unordered_map<std::string, std::string> s_en;
        std::unordered_map<std::string, std::string> s_cur;
        std::string s_lang = "en";
        std::mutex s_mutex;

        void flatten_table(const toml::table& tbl, const std::string& prefix,
                           std::unordered_map<std::string, std::string>& out)
        {
            for (auto&& [k, node] : tbl)
            {
                std::string key = std::string(k.str());
                std::string full = prefix.empty() ? key : (prefix + "." + key);
                if (node.is_table())
                {
                    flatten_table(*node.as_table(), full, out);
                }
                else if (auto sval = node.value<std::string>())
                {
                    out[full] = *sval;
                }
                // Arrays and other types are ignored for simplicity in this PoC
            }
        }

        std::unordered_map<std::string, std::string> load_file(const fs::path& p)
        {
            std::unordered_map<std::string, std::string> r;
            std::error_code ec;
            if (!fs::exists(p, ec))
            {
                PLOG_WARNING << "i18n file not found: " << p.string();
                return r;
            }
            try
            {
                toml::table t = toml::parse_file(p.string());
                flatten_table(t, "", r);
            }
            catch (const toml::parse_error& pe)
            {
                PLOG_WARNING << "Failed to parse i18n file '" << p.string() << "': " << pe.description();
            }
            return r;
        }

        std::string replace_named(const std::string& s, const std::unordered_map<std::string, std::string>& args)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); )
            {
                if (s[i] == '{')
                {
                    size_t j = s.find('}', i + 1);
                    if (j != std::string::npos)
                    {
                        std::string name = s.substr(i + 1, j - (i + 1));
                        auto it = args.find(name);
                        if (it != args.end())
                            out += it->second;
                        else
                            out += s.substr(i, j - i + 1); // keep as-is
                        i = j + 1;
                        continue;
                    }
                }
                out.push_back(s[i]);
                ++i;
            }
            return out;
        }

        void load_language_locked(const std::string& lang)
        {
            s_en = load_file("assets/i18n/en.toml");
            if (s_en.empty())
            {
                PLOG_WARNING << "English fallback assets/i18n/en.toml is empty or missing.";
            }

            s_cur.clear();
            if (lang == "en")
            {
                s_cur = s_en; // copy for faster lookups
            }
            else
            {
                fs::path alt = fs::path("assets/i18n") / (lang + ".toml");
                s_cur = load_file(alt);
                if (s_cur.empty())
                {
                    PLOG_WARNING << "i18n language '" << lang << "' not found; using English fallback.";
                }
            }
        }
    } // namespace

    void init(const std::string& lang_code)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_lang = lang_code.empty() ? std::string("en") : lang_code;
        load_language_locked(s_lang);
        PLOG_INFO << "i18n initialized with language: " << s_lang;
    }

    bool set_language(const std::string& lang_code)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        std::string next = lang_code.empty() ? std::string("en") : lang_code;
        if (next == s_lang)
            return true;
        load_language_locked(next);
        s_lang = next;
        return true;
    }

    const std::string& current_language()
    {
        return s_lang;
    }

    const std::string& get_str(const std::string& key)
    {
        // Avoid locking for every lookup in this PoC; assume set_language happens on UI thread
        auto it = s_cur.find(key);
        if (it != s_cur.end()) return it->second;
        auto ie = s_en.find(key);
        if (ie != s_en.end()) return ie->second;
        static std::string s_dummy;
        s_dummy = key; // return key as visible fallback
        return s_dummy;
    }

    const char* get(const char* key)
    {
        return get_str(std::string(key)).c_str();
    }

    std::string format(const std::string& key, const std::unordered_map<std::string, std::string>& args)
    {
        const std::string& base = get_str(key);
        return replace_named(base, args);
    }
}