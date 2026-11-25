#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/functions.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sheetcore::func_text_date_info {

namespace {

// ---- Lightweight local XML / XPath-subset evaluator for FILTERXML(xml, xpath) ----
struct XmlNode {
    std::string tag;
    std::unordered_map<std::string, std::string> attrs;
    std::string text;
    std::vector<XmlNode> children;
};

void skipWs(std::string_view s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

std::string xmlUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            auto semi = s.find(';', i);
            if (semi != std::string_view::npos) {
                auto ent = s.substr(i + 1, semi - i - 1);
                if (ent == "lt") { out.push_back('<'); i = semi; continue; }
                if (ent == "gt") { out.push_back('>'); i = semi; continue; }
                if (ent == "amp") { out.push_back('&'); i = semi; continue; }
                if (ent == "quot") { out.push_back('"'); i = semi; continue; }
                if (ent == "apos") { out.push_back('\''); i = semi; continue; }
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

bool parseXmlElement(std::string_view s, std::size_t& i, XmlNode& node) {
    skipWs(s, i);
    while (i + 1 < s.size() && s[i] == '<' && (s[i + 1] == '?' || s[i + 1] == '!')) {
        auto end = (s[i + 1] == '?') ? s.find("?>", i + 2) : s.find("-->", i + 2);
        if (end == std::string_view::npos) return false;
        i = end + (s[i + 1] == '?' ? 2 : 3);
        skipWs(s, i);
    }
    if (i >= s.size() || s[i] != '<') return false;
    ++i;
    std::size_t nameStart = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
    node.tag = std::string(s.substr(nameStart, i - nameStart));
    if (node.tag.empty()) return false;

    while (i < s.size()) {
        skipWs(s, i);
        if (i >= s.size()) return false;
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '>') { i += 2; return true; }
        if (s[i] == '>') { ++i; break; }
        std::size_t aStart = i;
        while (i < s.size() && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>') ++i;
        std::string aName(s.substr(aStart, i - aStart));
        skipWs(s, i);
        if (i >= s.size() || s[i] != '=') return false;
        ++i;
        skipWs(s, i);
        if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return false;
        char q = s[i++];
        std::size_t vStart = i;
        while (i < s.size() && s[i] != q) ++i;
        if (i >= s.size()) return false;
        node.attrs[std::move(aName)] = xmlUnescape(s.substr(vStart, i - vStart));
        ++i;
    }

    while (i < s.size()) {
        if (s[i] == '<') {
            if (i + 1 < s.size() && s[i + 1] == '/') {
                auto end = s.find('>', i + 2);
                if (end == std::string_view::npos) return false;
                i = end + 1;
                return true;
            }
            if (i + 1 < s.size() && (s[i + 1] == '!' || s[i + 1] == '?')) {
                auto end = s.find('>', i + 2);
                if (end == std::string_view::npos) return false;
                i = end + 1;
                continue;
            }
            XmlNode child;
            if (!parseXmlElement(s, i, child)) return false;
            node.children.push_back(std::move(child));
        } else {
            std::size_t tStart = i;
            while (i < s.size() && s[i] != '<') ++i;
            node.text += xmlUnescape(s.substr(tStart, i - tStart));
        }
    }
    return false;
}

std::string nodeFullText(const XmlNode& n) {
    if (n.children.empty()) return n.text;
    std::string out = n.text;
    for (const auto& c : n.children) out += nodeFullText(c);
    return out;
}

struct XPathStep {
    bool recursive = false;
    std::string tag; // "*" or tag name or "@attr"
    std::string predAttr;
    std::string predVal;
    int predIndex = -1; // 1-based
};

std::vector<XPathStep> parseXPath(std::string_view xp) {
    std::vector<XPathStep> steps;
    std::size_t i = 0;
    while (i < xp.size()) {
        XPathStep st;
        if (i + 1 < xp.size() && xp[i] == '/' && xp[i + 1] == '/') {
            st.recursive = true;
            i += 2;
        } else if (xp[i] == '/') {
            ++i;
        }
        std::size_t start = i;
        while (i < xp.size() && xp[i] != '/' && xp[i] != '[') ++i;
        st.tag = std::string(xp.substr(start, i - start));
        if (i < xp.size() && xp[i] == '[') {
            ++i;
            auto end = xp.find(']', i);
            if (end != std::string_view::npos) {
                std::string_view pred = xp.substr(i, end - i);
                if (!pred.empty() && pred[0] == '@') {
                    auto eq = pred.find('=');
                    if (eq != std::string_view::npos) {
                        st.predAttr = std::string(pred.substr(1, eq - 1));
                        std::string_view v = pred.substr(eq + 1);
                        if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"')) v = v.substr(1, v.size() - 2);
                        st.predVal = std::string(v);
                    }
                } else {
                    try { st.predIndex = std::stoi(std::string(pred)); } catch (...) {}
                }
                i = end + 1;
            }
        }
        if (!st.tag.empty()) steps.push_back(std::move(st));
    }
    return steps;
}

void collectDescendants(const XmlNode& n, const std::string& tag, std::vector<const XmlNode*>& out) {
    if (tag == "*" || n.tag == tag) out.push_back(&n);
    for (const auto& c : n.children) collectDescendants(c, tag, out);
}

formula::EvalArg fnFilterXml(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string xml = toDisplayString(a[0].scalar);
    std::string xpath = toDisplayString(a[1].scalar);
    XmlNode root;
    std::size_t pos = 0;
    if (!parseXmlElement(xml, pos, root)) return formula::makeScalar(ErrorValue{ErrorType::Value});
    auto steps = parseXPath(xpath);
    if (steps.empty()) return formula::makeScalar(ErrorValue{ErrorType::Value});

    XmlNode doc;
    doc.children.push_back(std::move(root));
    std::vector<const XmlNode*> cur{&doc};
    std::vector<std::string> results;

    for (std::size_t si = 0; si < steps.size(); ++si) {
        const auto& st = steps[si];
        if (!st.tag.empty() && st.tag[0] == '@') {
            std::string attrName = st.tag.substr(1);
            for (const XmlNode* n : cur) {
                if (auto it = n->attrs.find(attrName); it != n->attrs.end()) results.push_back(it->second);
            }
            cur.clear();
            break;
        }
        std::vector<const XmlNode*> next;
        for (const XmlNode* n : cur) {
            std::vector<const XmlNode*> cand;
            if (st.recursive) {
                for (const auto& c : n->children) collectDescendants(c, st.tag, cand);
            } else {
                for (const auto& c : n->children) {
                    if (st.tag == "*" || c.tag == st.tag) cand.push_back(&c);
                }
            }
            if (!st.predAttr.empty()) {
                for (const XmlNode* c : cand) {
                    auto it = c->attrs.find(st.predAttr);
                    if (it != c->attrs.end() && it->second == st.predVal) next.push_back(c);
                }
            } else if (st.predIndex >= 1) {
                if (static_cast<std::size_t>(st.predIndex) <= cand.size()) {
                    next.push_back(cand[static_cast<std::size_t>(st.predIndex - 1)]);
                }
            } else {
                next.insert(next.end(), cand.begin(), cand.end());
            }
        }
        cur = std::move(next);
    }

    if (results.empty()) {
        for (const XmlNode* n : cur) results.push_back(nodeFullText(*n));
    }
    if (results.empty()) return formula::makeScalar(ErrorValue{ErrorType::Value});
    formula::ArrayValue out(results.size(), 1);
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (auto num = toNumber(results[i])) out.at(i, 0) = *num;
        else out.at(i, 0) = results[i];
    }
    return formula::makeArray(std::move(out));
}

// ---- Offline script-based language detector for DETECTLANGUAGE(text) ----
CellValue fnDetectLanguage(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string s = toDisplayString(a[0].scalar);
    if (s.empty()) return ErrorValue{ErrorType::Value};
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        uint32_t cp = c0;
        std::size_t step = 1;
        if ((c0 & 0xE0u) == 0xC0u && i + 1 < s.size()) {
            cp = ((c0 & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            step = 2;
        } else if ((c0 & 0xF0u) == 0xE0u && i + 2 < s.size()) {
            cp = ((c0 & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            step = 3;
        }
        i += step;
        if (cp >= 0x3040 && cp <= 0x30FF) return std::string("ja");
        if (cp >= 0xAC00 && cp <= 0xD7AF) return std::string("ko");
        if (cp >= 0x4E00 && cp <= 0x9FFF) return std::string("zh-Hans");
        if (cp >= 0x0400 && cp <= 0x04FF) return std::string("ru");
        if (cp >= 0x0600 && cp <= 0x06FF) return std::string("ar");
        if (cp >= 0x0590 && cp <= 0x05FF) return std::string("he");
        if (cp >= 0x0370 && cp <= 0x03FF) return std::string("el");
        if (cp >= 0x0E00 && cp <= 0x0E7F) return std::string("th");
        if (cp >= 0x0900 && cp <= 0x097F) return std::string("hi");
    }
    return std::string("en");
}

// ---- IMAGE(source, [alt_text], [sizing], [height], [width]) ----
CellValue fnImage(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    std::string src = toDisplayString(a[0].scalar);
    if (src.rfind("https://", 0) != 0 && src.rfind("http://", 0) != 0) {
        return ErrorValue{ErrorType::Value};
    }
    if (a.size() >= 2 && !a[1].isOmitted() && !isEmpty(a[1].scalar)) {
        return toDisplayString(a[1].scalar);
    }
    return src;
}

} // namespace

void registerExternalFunctions(formula::FunctionRegistry& registry) {
    registry.registerArrayFunction("FILTERXML", 2, 2, fnFilterXml);
    registry.registerFunction({"DETECTLANGUAGE", 1, 1, fnDetectLanguage});
    registry.registerFunction({"IMAGE", 1, 5, fnImage});

    // External / OLAP / Cloud / Host-dependent functions (return standard Excel error codes offline)
    auto connectErr = [](const std::vector<formula::EvalArg>&, const formula::EvalContext&) -> CellValue {
        return ErrorValue{ErrorType::Connect};
    };
    auto blockedErr = [](const std::vector<formula::EvalArg>&, const formula::EvalContext&) -> CellValue {
        return ErrorValue{ErrorType::Blocked};
    };

    registry.registerFunction({"CUBEKPIMEMBER", 3, 4, connectErr});
    registry.registerFunction({"CUBEMEMBER", 2, 3, connectErr});
    registry.registerFunction({"CUBEMEMBERPROPERTY", 3, 3, connectErr});
    registry.registerFunction({"CUBERANKEDMEMBER", 3, 4, connectErr});
    registry.registerFunction({"CUBESET", 2, 5, connectErr});
    registry.registerFunction({"CUBESETCOUNT", 1, 1, connectErr});
    registry.registerFunction({"CUBEVALUE", 1, 255, connectErr});

    registry.registerFunction({"STOCKHISTORY", 2, 11, connectErr});
    registry.registerFunction({"TRANSLATE", 1, 3, connectErr});
    registry.registerFunction({"RTD", 3, 255, connectErr});
    registry.registerFunction({"WEBSERVICE", 1, 1, blockedErr});
    registry.registerFunction({"CALL", 1, 255, blockedErr});
    registry.registerFunction({"REGISTER.ID", 2, 3, blockedErr});
    registry.registerFunction({"GETPIVOTDATA", 2, 255, [](const auto&, const auto&) -> CellValue {
        return ErrorValue{ErrorType::Ref};
    }});
    registry.registerFunction({"FIELDVALUE", 2, 2, [](const auto&, const auto&) -> CellValue {
        return ErrorValue{ErrorType::Field};
    }});
}

} // namespace sheetcore::func_text_date_info
