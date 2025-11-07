#include "ConfigManager.hpp"
#include "../utils/ErrorReporter.hpp"

#include <toml++/toml.h>
#include <plog/Log.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>

namespace fs = std::filesystem;

static long long file_mtime_ms(const fs::path& p)
{
    std::error_code ec;
    auto tp = fs::last_write_time(p, ec);
    if (ec)
        return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        tp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
}

ConfigManager::ConfigManager()
{
    config_path_ = "config.toml";
    last_mtime_ = file_mtime_ms(config_path_);
}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::registerTable(const std::string& path, TableCallbacks cb, std::vector<std::string> ownedKeys)
{
    for (const auto& handler : handlers_)
    {
        if (handler.path == path)
        {
            for (const auto& key : ownedKeys)
            {
                for (const auto& existingKey : handler.ownedKeys)
                {
                    if (key == existingKey)
                    {
                        last_error_ = "Duplicate ownership: key '" + key + "' at path '" + path + "' already registered";
                        PLOG_ERROR << last_error_;
                        return false;
                    }
                }
            }
        }
    }
    
    handlers_.push_back({path, std::move(cb), std::move(ownedKeys)});
    return true;
}

bool ConfigManager::load()
{
    last_error_.clear();
    std::ifstream ifs(config_path_, std::ios::binary);
    if (!ifs)
    {
        root_ = std::make_unique<toml::table>();
        return true;
    }
    
    try
    {
        root_ = std::make_unique<toml::table>(toml::parse(ifs));
        
        for (const auto& handler : handlers_)
        {
            const toml::table* section = resolveTablePath(*root_, handler.path);
            if (section)
            {
                handler.callbacks.load(*section);
            }
            else
            {
                toml::table empty;
                handler.callbacks.load(empty);
            }
        }
        
        last_mtime_ = file_mtime_ms(config_path_);
        return true;
    }
    catch (const toml::parse_error& pe)
    {
        last_error_ = std::string("config parse error: ") + std::string(pe.description());
        PLOG_WARNING << last_error_;
        
        std::string error_details = "TOML parse error";
        if (pe.source().begin.line > 0)
        {
            error_details = "Error at line " + std::to_string(pe.source().begin.line) + ": " + std::string(pe.description());
        }
        else
        {
            error_details = std::string(pe.description());
        }
        
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                                            "Configuration file has errors. Using defaults for invalid entries.",
                                            error_details + "\nFile: " + config_path_);
        return false;
    }
}

bool ConfigManager::reloadIfChanged()
{
    fs::path p(config_path_);
    auto mtime = file_mtime_ms(p);
    if (mtime == 0 || mtime == last_mtime_)
        return false;
    
    if (load())
    {
        PLOG_INFO << "Config reloaded from " << config_path_;
        return true;
    }
    else
    {
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration, "Failed to reload configuration",
                                            last_error_.empty() ? std::string("See logs for details") : last_error_);
        return false;
    }
}

bool ConfigManager::save()
{
    last_error_.clear();
    
    if (!root_)
    {
        root_ = std::make_unique<toml::table>();
    }
    
    toml::table output = *root_;
    for (const auto& handler : handlers_)
    {
        toml::table handlerTable = handler.callbacks.save();
        
        toml::table* target = resolveTablePath(output, handler.path);
        if (!target)
        {
            target = &output;
        }
        
        for (auto it = handlerTable.begin(); it != handlerTable.end(); )
        {
            const auto& key = it->first;
            bool isOwned = false;
            for (const auto& ownedKey : handler.ownedKeys)
            {
                if (key == ownedKey)
                {
                    isOwned = true;
                    break;
                }
            }
            
            if (!isOwned)
            {
                PLOG_WARNING << "Handler at path '" << handler.path << "' returned unexpected key '" 
                             << key << "' (not in ownedKeys); stripping it";
                it = handlerTable.erase(it);
            }
            else
            {
                ++it;
            }
        }
        
        for (const auto& key : handler.ownedKeys)
        {
            if (handlerTable.contains(key))
            {
                target->insert_or_assign(key, handlerTable[key]);
            }
            else
            {
                target->erase(key);
            }
        }
    }
    
    std::string tmp = config_path_ + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary);
    if (!ofs)
    {
        last_error_ = "Failed to open temp file for writing";
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
                                          "Could not create temporary file for writing: " + tmp);
        return false;
    }
    ofs << output;
    ofs.flush();
    ofs.close();
    
    std::error_code ec;
    fs::rename(tmp, config_path_, ec);
    if (ec)
    {
        last_error_ = std::string("Failed to rename: ") + ec.message();
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Failed to save configuration",
                                          "Could not rename temporary file: " + ec.message());
        return false;
    }
    
    last_mtime_ = file_mtime_ms(config_path_);
    *root_ = output;
    PLOG_INFO << "Saved config to " << config_path_;
    return true;
}

const toml::table& ConfigManager::root() const
{
    static const toml::table empty;
    return root_ ? *root_ : empty;
}

toml::table* ConfigManager::resolveTablePath(toml::table& root, const std::string& path)
{
    if (path.empty())
        return &root;
    
    std::istringstream ss(path);
    std::string segment;
    toml::table* current = &root;
    
    while (std::getline(ss, segment, '.'))
    {
        if (segment.empty())
        {
            PLOG_WARNING << "Invalid path segment (empty) in path: " << path;
            return nullptr;
        }
        
        auto it = current->find(segment);
        if (it == current->end())
        {
            auto [inserted_it, success] = current->insert(segment, toml::table{});
            if (!success)
            {
                PLOG_WARNING << "Failed to create table at path segment: " << segment;
                return nullptr;
            }
            current = inserted_it->second.as_table();
        }
        else if (auto* tbl = it->second.as_table())
        {
            current = tbl;
        }
        else
        {
            PLOG_WARNING << "Path segment '" << segment << "' exists but is not a table";
            return nullptr;
        }
    }
    
    return current;
}

const toml::table* ConfigManager::resolveTablePath(const toml::table& root, const std::string& path) const
{
    if (path.empty())
        return &root;
    
    std::istringstream ss(path);
    std::string segment;
    const toml::table* current = &root;
    
    while (std::getline(ss, segment, '.'))
    {
        if (segment.empty())
        {
            PLOG_WARNING << "Invalid path segment (empty) in path: " << path;
            return nullptr;
        }
        
        auto it = current->find(segment);
        if (it == current->end())
            return nullptr;
        
        auto* tbl = it->second.as_table();
        if (!tbl)
        {
            PLOG_WARNING << "Path segment '" << segment << "' exists but is not a table";
            return nullptr;
        }
        
        current = tbl;
    }
    
    return current;
}
