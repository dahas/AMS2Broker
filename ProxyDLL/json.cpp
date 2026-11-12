#include "pch.h"
#include "json.h"
#include <cctype>

std::string StripComments(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') i++;
        }
        else {
            out += s[i++];
        }
    }
    return out;
}

static std::string InnerObject(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); i++) {
        if (s[i] == '{') depth++;
        else if (s[i] == '}') {
            if (--depth == 0) return s.substr(open + 1, i - open - 1);
        }
    }
    return "";
}

static size_t FindObjectForKey(const std::string& scope, const std::string& key) {
    std::string quoted = "\"" + key + "\"";
    size_t k = scope.find(quoted);
    if (k == std::string::npos) return std::string::npos;
    size_t b = scope.find_first_not_of(" \t\r\n:", k + quoted.size());
    if (b == std::string::npos || scope[b] != '{') return std::string::npos;
    return b;
}

std::string GetValueAtPath(const std::string& json,
    const std::vector<std::string>& path)
{
    if (path.empty()) return "";

    std::string scope = json;

    for (size_t i = 0; i + 1 < path.size(); i++) {
        size_t b = FindObjectForKey(scope, path[i]);
        if (b == std::string::npos) return "";
        scope = InnerObject(scope, b);
        if (scope.empty()) return "";
    }

    const std::string& key = path.back();
    std::string quoted = "\"" + key + "\"";
    size_t k = scope.find(quoted);
    if (k == std::string::npos) return "";
    size_t c = scope.find(':', k + quoted.size());
    if (c == std::string::npos) return "";
    size_t v = scope.find_first_not_of(" \t\r\n", c + 1);
    if (v == std::string::npos) return "";

    if (scope[v] == '"') {
        size_t e = scope.find('"', v + 1);
        if (e == std::string::npos) return "";
        return scope.substr(v + 1, e - v - 1);
    }

    size_t e = scope.find_first_of(",}\r\n", v);
    if (e == std::string::npos) e = scope.size();
    while (e > v && std::isspace((unsigned char)scope[e - 1])) e--;
    return scope.substr(v, e - v);
}

bool GetIntAtPath(const std::string& json,
    const std::vector<std::string>& path,
    long long& out)
{
    std::string raw = GetValueAtPath(json, path);
    if (raw.empty()) return false;
    if (raw == "true") { out = 1; return true; }
    if (raw == "false") { out = 0; return true; }
    try {
        size_t used = 0;
        out = std::stoll(raw, &used);
        return used > 0;
    }
    catch (...) {
        return false;
    }
}

bool ParseClassStates(const std::string& json, std::map<int, int>& out) {
    size_t classPos = json.find("\"ClassStates\"");
    if (classPos == std::string::npos) return true;

    size_t startBrace = json.find('{', classPos);
    if (startBrace == std::string::npos) return true;
    size_t endBrace = json.find('}', startBrace);
    if (endBrace == std::string::npos) return true;

    std::string classStr = json.substr(startBrace + 1, endBrace - startBrace - 1);

    size_t pos = 0;
    while (pos < classStr.length()) {
        size_t qs = classStr.find('"', pos);
        if (qs == std::string::npos) break;
        size_t qe = classStr.find('"', qs + 1);
        if (qe == std::string::npos) break;
        std::string key = classStr.substr(qs + 1, qe - qs - 1);

        size_t colon = classStr.find(':', qe);
        if (colon == std::string::npos) break;
        size_t v = classStr.find_first_not_of(" \t\r\n", colon + 1);
        if (v == std::string::npos) break;
        if (classStr[v] == '"') {
            v++;
            if (v >= classStr.length()) break;
        }

        int val = -1;
        size_t consumed = 1;
        if (classStr.compare(v, 4, "true") == 0) { val = 1; consumed = 4; }
        else if (classStr.compare(v, 5, "false") == 0) { val = 0; consumed = 5; }
        else if (classStr[v] == '1') { val = 1; }
        else if (classStr[v] == '0') { val = 0; }

        pos = v + consumed;
        if (val < 0) continue;

        int classNumber = 0;
        try { classNumber = std::stoi(key); }
        catch (...) { continue; }

        if (classNumber < 1 || classNumber > 89) continue;  // MAX_CLASSES
        out[classNumber] = val;
    }
    return true;
}