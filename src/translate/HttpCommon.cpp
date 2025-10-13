 #include "HttpCommon.hpp"

#include <cpr/cpr.h>
#include <cctype>

 namespace {
 
 inline void apply_common(cpr::Session& s, const translate::SessionConfig& cfg) {
     s.SetConnectTimeout(cpr::ConnectTimeout{cfg.connect_timeout_ms});
     s.SetTimeout(cpr::Timeout{cfg.timeout_ms});
     if (cfg.cancel_flag) {
         s.SetProgressCallback(cpr::ProgressCallback(
             [](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t userdata) -> bool {
                 auto flag = reinterpret_cast<std::atomic<bool>*>(userdata);
                 return flag && flag->load();
             }, reinterpret_cast<intptr_t>(cfg.cancel_flag)));
     }
 }

 inline cpr::Header make_header(const std::vector<translate::Header>& headers, bool ensure_json) {
     cpr::Header h;
     bool has_ct = false;
     for (auto& kv : headers) {
        if (!has_ct && kv.name.size() == 12) {
            bool eq = true;
            const char* ct = "Content-Type";
            for (size_t i = 0; i < 12; ++i) {
                char a = static_cast<char>(std::tolower(static_cast<unsigned char>(kv.name[i])));
                char b = static_cast<char>(std::tolower(static_cast<unsigned char>(ct[i])));
                if (a != b) { eq = false; break; }
            }
            if (eq) has_ct = true;
        }
         h.emplace(kv.name, kv.value);
     }
     if (ensure_json && !has_ct) h.emplace("Content-Type", "application/json");
     return h;
 }

 inline std::string url_escape(const std::string& s) {
     std::string out; out.reserve(s.size() * 3);
     const char* hex = "0123456789ABCDEF";
     for (unsigned char c : s) {
         if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c=='-'||c=='_'||c=='.'||c=='~') {
             out.push_back(static_cast<char>(c));
         } else {
             out.push_back('%');
             out.push_back(hex[c >> 4]);
             out.push_back(hex[c & 0x0F]);
         }
     }
     return out;
 }

 } // namespace

 namespace translate {

 HttpResponse post_json(const std::string& url,
                        const std::string& body,
                        const std::vector<Header>& headers,
                        const SessionConfig& cfg) {
     cpr::Session s;
     s.SetUrl(cpr::Url{url});
     s.SetHeader(make_header(headers, /*ensure_json*/true));
     s.SetBody(cpr::Body{body});
     apply_common(s, cfg);
     auto r = s.Post();
     HttpResponse hr;
     if (r.error) { hr.error = r.error.message; return hr; }
     hr.status_code = r.status_code;
     hr.text = std::move(r.text);
     return hr;
 }

 HttpResponse post_form(const std::string& url,
                        const std::vector<std::pair<std::string, std::string>>& fields,
                        const SessionConfig& cfg,
                        const std::vector<Header>& headers) {
     // Build x-www-form-urlencoded body manually to accept dynamic fields
     std::string body;
     for (size_t i = 0; i < fields.size(); ++i) {
         if (i) body.push_back('&');
         body += url_escape(fields[i].first);
         body.push_back('=');
         body += url_escape(fields[i].second);
     }
     cpr::Session s;
     s.SetUrl(cpr::Url{url});
     // Merge provided headers; ensure Content-Type is set
     cpr::Header h;
     bool has_ct = false;
     for (auto& kv : headers) {
         // case-insensitive check for Content-Type
         if (!has_ct && kv.name.size() == 12) {
             bool eq = true; const char* ct = "Content-Type";
             for (size_t i = 0; i < 12; ++i) {
                 char a = static_cast<char>(std::tolower(static_cast<unsigned char>(kv.name[i])));
                 char b = static_cast<char>(std::tolower(static_cast<unsigned char>(ct[i])));
                 if (a != b) { eq = false; break; }
             }
             if (eq) has_ct = true;
         }
         h.emplace(kv.name, kv.value);
     }
     if (!has_ct) h.emplace("Content-Type", "application/x-www-form-urlencoded");
     s.SetHeader(std::move(h));
     s.SetBody(cpr::Body{body});
     apply_common(s, cfg);
     auto r = s.Post();
     HttpResponse hr;
     if (r.error) { hr.error = r.error.message; return hr; }
     hr.status_code = r.status_code;
     hr.text = std::move(r.text);
     return hr;
 }

 HttpResponse get(const std::string& url,
                  const std::vector<Header>& headers,
                  const SessionConfig& cfg) {
     cpr::Session s;
     s.SetUrl(cpr::Url{url});
     s.SetHeader(make_header(headers, /*ensure_json*/false));
     apply_common(s, cfg);
     auto r = s.Get();
     HttpResponse hr;
     if (r.error) { hr.error = r.error.message; return hr; }
     hr.status_code = r.status_code;
     hr.text = std::move(r.text);
     return hr;
 }

 } // namespace translate
