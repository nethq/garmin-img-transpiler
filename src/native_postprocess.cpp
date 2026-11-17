// SPDX-License-Identifier: GPL-3.0-or-later
// Streaming native route attachment, relation-ID repair and semantic audit.
// Expat validates XML. Unchanged entities are copied byte-for-byte.
#include <expat.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <numeric>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
#define XML_GetCurrentByteIndex XML_GetCurrentByteIndex64
#define XML_GetCurrentByteCount XML_GetCurrentByteCount64
#define XML_GetCurrentLineNumber XML_GetCurrentLineNumber64
#endif

using Id = std::int64_t;
using Tags = std::map<std::string, std::string>;
using Counts = std::map<std::string, std::uint64_t>;
constexpr Id relationLimit = Id{1} << 27;

std::string value(const Tags& tags, const std::string& key) {
    auto it = tags.find(key);
    return it == tags.end() ? "" : it->second;
}
std::string attribute(const XML_Char** attrs, const std::string& key) {
    for (auto p = attrs; *p; p += 2) if (key == p[0]) return p[1];
    return "";
}
std::string json(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 32) {
                const char* hex = "0123456789abcdef";
                out << "\\u00" << hex[ch >> 4] << hex[ch & 15];
            } else out << ch;
        }
    }
    return out.str() + '"';
}
std::string jsonCounts(const Counts& counts) {
    std::string out = "{";
    for (const auto& item : counts) {
        if (out.size() > 1) out += ',';
        out += json(item.first) + ':' + std::to_string(item.second);
    }
    return out + '}';
}
std::unordered_set<std::string> readCodes(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read type list: " + path);
    std::unordered_set<std::string> result;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) result.insert(line);
    }
    return result;
}
struct Edge {
    Id a, b;
    bool operator==(const Edge& other) const { return a == other.a && b == other.b; }
};
struct EdgeHash {
    std::size_t operator()(const Edge& edge) const {
        auto a = std::hash<Id>{}(edge.a), b = std::hash<Id>{}(edge.b);
        return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    }
};
template<class Visitor> void eachEdge(const std::vector<Id>& refs, Visitor visitor) {
    for (std::size_t i = 1; i < refs.size(); ++i) {
        if (refs[i-1] != refs[i]) visitor(Edge{std::min(refs[i-1], refs[i]), std::max(refs[i-1], refs[i])});
    }
}
struct Audit {
    std::uint64_t pois = 0, synthetic = 0, unresolved = 0;
    Counts seen, missing, unknown, fallback, annotations, routes, categories;
    std::map<std::string, std::size_t> categoryOrder;
    std::map<std::string, std::vector<std::array<std::string, 3>>> samples;
    std::unordered_set<std::string> configured;
    void node(const Tags& tags, const std::string& lat, const std::string& lon) {
        if (value(tags, "garmin:feature") != "poi") return;
        ++pois;
        auto code = value(tags, "garmin:type");
        ++seen[code];
        if (!configured.count(code)) ++missing[code];
        if (value(tags, "bgm:synthetic_fallback") == "place_locality") {
            ++synthetic;
            ++fallback[code];
        }
        static const std::set<std::string> semantic = {
            "amenity","tourism","emergency","natural","man_made","place","historic","shop",
            "leisure","building","waterway","railway","power","aeroway","highway","ele",
            "hazard","ford","water","water_source","landuse","barrier","aerialway","craft","geological"
        };
        bool classified = false;
        for (const auto& item : tags) if (semantic.count(item.first)) classified = true;
        if (code == "0x6600" && tags.count("ele")) ++annotations["spot_height"];
        else if (!classified) { ++unknown[code]; ++unresolved; }
        auto& examples = samples[code];
        if (examples.size() < 5) examples.push_back({lat, lon, value(tags, "name")});
        for (const auto& key : {"amenity","tourism","emergency","natural","man_made","building","historic","shop","leisure"}) {
            auto it = tags.find(key);
            if (it != tags.end()) {
                auto category = it->first + '=' + it->second;
                categoryOrder.emplace(category, categoryOrder.size());
                ++categories[category];
            }
        }
    }
    void write(const std::string& path, bool strict) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("Cannot write audit: " + path);
        out << "{\n\"status\":" << json((missing.empty() && unknown.empty()) ? "PASS" : (strict ? "FAIL" : "WARN"))
            << ",\n\"total_pois\":" << pois << ",\n\"mapped_types\":" << configured.size()
            << ",\n\"observed_types\":" << seen.size() << ",\n\"source_codes_not_in_config\":" << jsonCounts(missing)
            << ",\n\"unclassified_counts\":" << jsonCounts(unknown) << ",\n\"unclassified_total\":" << unresolved
            << ",\n\"synthetic_locality_fallback_total\":" << synthetic
            << ",\n\"synthetic_locality_counts\":" << jsonCounts(fallback)
            << ",\n\"annotations_not_missing_pois\":" << jsonCounts(annotations)
            << ",\n\"route_relation_counts\":" << jsonCounts(routes);
        std::vector<std::pair<std::string, std::uint64_t>> top(categories.begin(), categories.end());
        std::stable_sort(top.begin(), top.end(), [&](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : categoryOrder.at(a.first) < categoryOrder.at(b.first);
        });
        if (top.size() > 100) top.resize(100);
        Counts selected(top.begin(), top.end());
        out << ",\n\"native_semantic_categories\":" << jsonCounts(selected) << ",\n\"sample_coordinates\":{";
        bool first = true;
        for (const auto& item : samples) {
            if (!first) out << ',';
            first = false;
            out << json(item.first) << ":[";
            bool firstSample = true;
            for (const auto& sample : item.second) {
                if (!firstSample) out << ',';
                firstSample = false;
                out << '[' << json(sample[0]) << ',' << json(sample[1]) << ',' << json(sample[2]) << ']';
            }
            out << ']';
        }
        out << "},\n\"warning\":\"OSM audit only; stock icons/search not guaranteed. Synthetic localities are private-map fallback, not valid public OSM classification. Elevation-only objects deliberately have no artificial POI icon.\"\n}\n";
        if (!out) throw std::runtime_error("Cannot complete audit: " + path);
    }
};
std::string xmlEscape(const std::string& value) {
    std::string result;
    for (char c : value) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\n': result += "&#10;"; break;
        case '\r': result += "&#13;"; break;
        case '\t': result += "&#9;"; break;
        default: result += c;
        }
    }
    return result;
}
std::string xmlTags(const Tags& tags) {
    std::string result;
    for (const auto& item : tags) if (!item.second.empty())
        result += "<tag k=\"" + xmlEscape(item.first) + "\" v=\"" + xmlEscape(item.second) + "\"/>";
    return result;
}
struct Rule { Tags attrs, wayTags; };
struct Profile {
    std::map<std::string, Rule> pois, lines;
    Tags widths;
    Rule* current = nullptr;
    static void start(void* data, const XML_Char* name, const XML_Char** attrs) {
        auto& self = *static_cast<Profile*>(data);
        std::string kind = name;
        if (kind == "hash") {
            self.widths[attribute(attrs,"code")] = attribute(attrs,"width");
        } else if (kind == "poi" || kind == "line") {
            auto code = attribute(attrs, "code");
            auto& rule = (kind == "poi" ? self.pois : self.lines)[code];
            for (auto a = attrs; *a; a += 2) rule.attrs[a[0]] = a[1];
            self.current = &rule;
        } else if (kind == "tag" && self.current) {
            self.current->wayTags[attribute(attrs, "k")] = attribute(attrs, "v");
        }
    }
    void read(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read compiled transform profile");
        std::string text((std::istreambuf_iterator<char>(input)), {});
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetUserData(parser, this);
        XML_SetStartElementHandler(parser, start);
        bool ok = XML_Parse(parser, text.data(), static_cast<int>(text.size()), true) == XML_STATUS_OK;
        XML_ParserFree(parser);
        if (!ok) throw std::runtime_error("Invalid compiled transform profile");
    }
};
// Exact decimal scaling, including exponent notation, with half-away-from-zero
// rounding. This matches Decimal(value).quantize(..., ROUND_HALF_UP).
std::string quantized(const std::string& text) {
    std::size_t at = 0;
    bool negative = !text.empty() && text[0] == '-';
    if (!text.empty() && (text[0] == '-' || text[0] == '+')) ++at;
    std::string digits;
    int decimal = 0;
    bool dot = false;
    for (; at < text.size() && text[at] != 'e' && text[at] != 'E'; ++at) {
        char ch = text[at];
        if (ch == '.' && !dot) { dot = true; continue; }
        if (ch < '0' || ch > '9') throw std::runtime_error("Invalid coordinate: " + text);
        digits += ch;
        if (!dot) ++decimal;
    }
    if (digits.empty()) throw std::runtime_error("Invalid coordinate: " + text);
    int exponent = 0;
    if (at < text.size()) {
        auto token = text.substr(at + 1);
        std::size_t used = 0;
        exponent = std::stoi(token,&used);
        if (used != token.size() || exponent < -10000 || exponent > 10000)
            throw std::runtime_error("Invalid coordinate exponent: " + text);
    }
    int scaledDigits = decimal + exponent + 7;
    std::uint64_t magnitude = 0;
    for (int i = 0; i < scaledDigits; ++i) {
        magnitude = magnitude * 10 + (i < static_cast<int>(digits.size()) ? digits[i] - '0' : 0);
        if (magnitude > 1800000000) throw std::runtime_error("Coordinate outside geographic range");
    }
    if (scaledDigits >= 0 && scaledDigits < static_cast<int>(digits.size()) && digits[scaledDigits] >= '5') ++magnitude;
    return (negative && magnitude ? "-" : "") + std::to_string(magnitude);
}
bool blankName(const std::string& name) {
    std::uint32_t code = 0;
    for (std::size_t i = 0; i < name.size();) {
        auto ch = static_cast<unsigned char>(name[i++]);
        if (ch < 128) code = ch;
        else {
            int extra = ch < 224 ? 1 : ch < 240 ? 2 : 3;
            code = ch & (extra == 1 ? 31 : extra == 2 ? 15 : 7);
            for (int j = 0; j < extra && i < name.size(); ++j)
                code = (code << 6) | (static_cast<unsigned char>(name[i++]) & 63);
        }
        bool space = (code >= 9 && code <= 13) || (code >= 28 && code <= 32) ||
            code == 0x85 || code == 0xa0 || code == 0x1680 || (code >= 0x2000 && code <= 0x200a) ||
            code == 0x2028 || code == 0x2029 || code == 0x202f || code == 0x205f || code == 0x3000;
        if (!space) return false;
    }
    return true;
}
std::string suffix(const Tags& tags, const Rule* rule, const std::string& lat, const std::string& lon, int defaultWidth) {
    int width = rule ? std::stoi(value(rule->attrs, "width")) : defaultWidth;
    int skip = rule ? std::stoi(value(rule->attrs, "skip")) : 0;
    std::string seed = value(tags,"garmin:mapset") + '|' + value(tags,"garmin:type") + '|' + quantized(lat) + '|' + quantized(lon);
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : seed) hash = (hash ^ byte) * 1099511628211ULL;
    for (int i = 0; i < skip; ++i) hash /= 36;
    std::string result(width, '0');
    for (int i = width - 1; i >= 0; --i) {
        result[i] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[hash % 36];
        hash /= 36;
    }
    return result;
}
struct Patch { std::uint64_t begin, end; std::string replacement; };
struct Processor {
    bool transform = false;
    Profile profile;
    int defaultWidth = 6;
    std::uint64_t ways = 0, emitted = 0, synthetic = 0;
    Counts gaps;
    struct RouteEdge { Id way, a, b; };
    std::map<std::string, std::vector<RouteEdge>> routeEdges;
    std::uint64_t tagBegin = 0;
    std::vector<Patch> tagSpans;
    bool attach = false, auditOnly = false, firstPass = true, strict = false;
    std::string inputPath, outputPath, reportPath, auditPath;
    std::unordered_set<std::string> routeTypes;
    Audit audit;
    std::unordered_map<Id, std::size_t> overlayLengths;
    std::unordered_map<Edge, std::vector<Id>, EdgeHash> lookup;
    struct Candidate { Id id; std::size_t segments, order; };
    std::unordered_map<Id, Candidate> physical;
    std::unordered_map<Id, std::unordered_map<Id, std::size_t>> matches;
    std::unordered_map<Id, Id> replacements, relationIds;
    std::unordered_set<Id> occupiedRelations;
    std::vector<Id> invalidRelations;
    bool replacementsReady = false, sawRelation = false;
    std::uint64_t relationCount = 0;
    XML_Parser parser = nullptr;
    int depth = 0;
    std::string kind, lat, lon, pending, error;
    Tags tags;
    Id currentId = 0;
    std::vector<Id> refs;
    std::uint64_t pendingBase = 0, consumed = 0;
    std::vector<Patch> patches;
    std::set<std::array<std::string, 3>> members;
    bool removeMember = false;
    std::uint64_t memberBegin = 0;
    std::ofstream output;
    ~Processor() { if (parser) XML_ParserFree(parser); }
    void fail(const std::string& message) { throw std::runtime_error(message); }
    void attributePatch(const std::string& key, Id newValue) {
        auto begin = static_cast<std::uint64_t>(XML_GetCurrentByteIndex(parser));
        auto length = static_cast<std::size_t>(XML_GetCurrentByteCount(parser));
        auto token = pending.substr(begin - pendingBase, length);
        std::size_t i = token.find_first_of(" \t\r\n");
        while (i != std::string::npos && i < token.size()) {
            i = token.find_first_not_of(" \t\r\n", i);
            if (i == std::string::npos || token[i] == '/' || token[i] == '>') break;
            auto nameEnd = token.find_first_of("= \t\r\n", i);
            if (nameEnd == std::string::npos) break;
            auto name = token.substr(i, nameEnd - i);
            auto equals = token.find('=', nameEnd);
            auto quote = token.find_first_of("\"'", equals + 1);
            auto end = token.find(token[quote], quote + 1);
            if (name == key) {
                patches.push_back({begin + quote + 1, begin + end, std::to_string(newValue)});
                return;
            }
            i = end + 1;
        }
        fail("Missing XML attribute to rewrite: " + key);
    }
    void flush(std::uint64_t end) {
        auto cursor = consumed;
        for (const auto& patch : patches) {
            if (patch.begin < cursor || patch.end > end) fail("Overlapping XML patches");
            output.write(pending.data() + cursor - pendingBase, patch.begin - cursor);
            output << patch.replacement;
            cursor = patch.end;
        }
        output.write(pending.data() + cursor - pendingBase, end - cursor);
        consumed = end;
        patches.clear();
        if (!output) fail("Failed writing OSM output");
    }
    void selectMatches() {
        if (replacementsReady) return;
        replacementsReady = true;
        for (const auto& overlay : matches) {
            std::vector<std::pair<Id, std::size_t>> counts(overlay.second.begin(), overlay.second.end());
            std::sort(counts.begin(), counts.end(), [&](const auto& a, const auto& b) {
                return a.second != b.second ? a.second > b.second : physical.at(a.first).order < physical.at(b.first).order;
            });
            auto nseg = overlayLengths.at(overlay.first);
            for (const auto& candidate : counts) {
                if (candidate.second * 10 >= nseg * 9 && physical.at(candidate.first).segments * 10 <= nseg * 11) {
                    replacements[overlay.first] = candidate.first;
                    break;
                }
            }
        }
    }
    void start(const XML_Char* name, const XML_Char** attrs) {
        ++depth;
        if (depth == 1) {
            if (std::string(name) != "osm") fail("Expected OSM XML root");
            return;
        }
        if (depth == 2) {
            kind = name;
            tags.clear(); refs.clear(); members.clear(); tagSpans.clear();
            if (kind == "way" || kind == "relation") currentId = std::stoll(attribute(attrs, "id"));
            if (firstPass) {
                if (kind == "way" && sawRelation) fail("Native postprocessor requires ways before relations");
                if (kind == "relation") {
                    sawRelation = true;
                    if (!occupiedRelations.insert(currentId).second) fail("Duplicate relation ID: " + std::to_string(currentId));
                    if (currentId <= 0 || currentId >= relationLimit) invalidRelations.push_back(currentId);
                }
            } else {
                if (kind == "node") { lat = attribute(attrs, "lat"); lon = attribute(attrs, "lon"); }
                if (kind == "relation" && transform) {
                    if (!occupiedRelations.insert(currentId).second) fail("Duplicate input relation ID");
                }
                if (kind == "relation" && !transform) {
                    selectMatches();
                    auto it = relationIds.find(currentId);
                    if (it != relationIds.end() && !auditOnly) attributePatch("id", it->second);
                }
            }
        } else if (depth == 3) {
            std::string child = name;
            if (child == "tag" && (kind == "way" || kind == "relation" || (!firstPass && kind == "node"))) {
                tags[attribute(attrs, "k")] = attribute(attrs, "v");
                tagBegin = XML_GetCurrentByteIndex(parser);
            } else if (child == "nd" && kind == "way" && (attach || transform)) {
                refs.push_back(std::stoll(attribute(attrs, "ref")));
            } else if (child == "member" && kind == "relation" && !firstPass && !auditOnly && !transform) {
                auto type = attribute(attrs, "type");
                Id ref = std::stoll(attribute(attrs, "ref"));
                Id changed = ref;
                // Relation tags follow members. First pass records which relations are hiking overlays.
                if (type == "way" && hikingRelations.count(currentId)) {
                    auto it = replacements.find(ref);
                    if (it != replacements.end()) changed = it->second;
                } else if (type == "relation") {
                    auto it = relationIds.find(ref);
                    if (it != relationIds.end()) changed = it->second;
                }
                removeMember = attach && hikingRelations.count(currentId) &&
                    !members.insert({type, std::to_string(changed), attribute(attrs, "role")}).second;
                memberBegin = XML_GetCurrentByteIndex(parser);
                if (!removeMember && changed != ref) attributePatch("ref", changed);
            }
        }
    }
    std::unordered_set<Id> hikingRelations;
    void rewriteTags() {
        for (auto it = tags.begin(); it != tags.end();) {
            if (it->second.empty()) it = tags.erase(it);
            else ++it;
        }
        for (const auto& span : tagSpans) patches.push_back(span);
        auto closing = static_cast<std::uint64_t>(XML_GetCurrentByteIndex(parser));
        patches.push_back({closing, closing, xmlTags(tags)});
    }
    void transformNode() {
        if (value(tags,"garmin:feature") != "poi") return;
        auto code = value(tags,"garmin:type");
        auto it = profile.pois.find(code);
        const Rule* rule = it == profile.pois.end() ? nullptr : &it->second;
        if (rule) {
            tags["bgm:legend_bg"] = value(rule->attrs,"legend");
            tags["bgm:priority"] = value(rule->attrs,"priority");
            tags.erase("bgm:synthetic_icon");
        } else ++gaps[code];
        tags.erase("bgm_poi");
        static const std::set<std::string> semantic = {
            "amenity","tourism","emergency","natural","man_made","place","historic","shop",
            "leisure","building","waterway","railway","power","aeroway","highway","hazard",
            "ford","water","water_source","landuse","barrier","aerialway","craft","military",
            "geological","public_transport","sport"
        };
        bool classified = false;
        for (const auto& tag : tags) if (semantic.count(tag.first)) classified = true;
        if (!classified && code != "0x6600" && !tags.count("ele") && (!rule || value(rule->attrs,"action") != "drop")) {
            tags["place"] = "locality";
            tags["bgm:synthetic_fallback"] = "place_locality";
            tags["bgm:classification"] = "unverified";
            ++synthetic;
        }
        auto name = value(tags,"name");
        bool blank = blankName(name);
        if (!code.empty() && (blank || value(tags,"bgm:generated_name") == "yes")) {
            std::string label = rule ? value(rule->attrs,"label") : "";
            if (label.empty()) label = "Garmin обект " + code;
            if (value(tags,"bgm:synthetic_fallback") == "place_locality") label = "Неуточнено";
            int width = profile.widths.count(code) ? std::stoi(profile.widths.at(code)) : defaultWidth;
            tags["name"] = label + " - " + suffix(tags,rule,lat,lon,width);
            tags["bgm:generated_name"] = "yes";
        }
        if (code == "0x6600" && value(tags,"bgm:generated_name") != "yes") tags.erase("bgm:lookup");
        rewriteTags();
    }
    void transformWay() {
        ++ways;
        if (value(tags,"garmin:feature") != "extended_polyline" && !tags.count("bgm_route")) return;
        tags.erase("bgm_route");
        auto code = value(tags,"garmin:type");
        auto it = profile.lines.find(code);
        if (it == profile.lines.end()) ++gaps[code];
        else {
            const auto& rule = it->second;
            tags["bgm:line_legend"] = value(rule.attrs,"legend");
            tags["bgm:translation_status"] = value(rule.attrs,"confidence");
            for (const auto& tag : rule.wayTags) tags[tag.first] = tag.second;
            if (value(rule.attrs,"mode") == "relation" && refs.size() >= 2)
                routeEdges[code].push_back({currentId,refs.front(),refs.back()});
        }
        rewriteTags();
    }
    void generateRelations() {
        std::size_t components = 0, largest = 0, candidates = 0;
        Id nextId = 1;
        std::string generated;
        for (auto& group : routeEdges) {
            auto& edges = group.second;
            std::stable_sort(edges.begin(),edges.end(),[](const auto& a, const auto& b) { return a.way < b.way; });
            candidates += edges.size();
            std::vector<std::size_t> parent(edges.size()), rank(edges.size());
            std::iota(parent.begin(), parent.end(), 0);
            auto root = [&](std::size_t at) {
                while (parent[at] != at) { parent[at] = parent[parent[at]]; at = parent[at]; }
                return at;
            };
            std::unordered_map<Id, std::size_t> endpoints;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                for (Id endpoint : {edges[i].a, edges[i].b}) {
                    auto pair = endpoints.emplace(endpoint,i);
                    if (!pair.second) {
                        auto a = root(i), b = root(pair.first->second);
                        if (a == b) continue;
                        if (rank[a] < rank[b]) std::swap(a,b);
                        parent[b] = a;
                        if (rank[a] == rank[b]) ++rank[a];
                    }
                }
            }
            // Component order follows first member encounter, as in the Python reference.
            std::unordered_map<std::size_t,std::size_t> componentIndex;
            std::vector<std::vector<Id>> groups;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                auto inserted = componentIndex.emplace(root(i),groups.size());
                if (inserted.second) groups.emplace_back();
                groups[inserted.first->second].push_back(edges[i].way);
            }
            const auto& rule = profile.lines.at(group.first).attrs;
            for (auto& members : groups) {
                ++components;
                largest = std::max(largest,members.size());
                while (occupiedRelations.count(nextId)) ++nextId;
                if (nextId >= relationLimit) fail("No available relation IDs below 2^27");
                generated += "<relation id=\"" + std::to_string(nextId++) + "\" version=\"1\">";
                std::sort(members.begin(),members.end());
                for (Id member : members) generated += "<member type=\"way\" ref=\"" + std::to_string(member) + "\" role=\"\"/>";
                Tags relationTags = {{"type","route"},{"route","hiking"},{"network",value(rule,"network")},
                    {"garmin:type",group.first},{"bgm:source_feature","extended_polyline"},{"bgm:confidence",value(rule,"confidence")},
                    {"osmc:symbol",value(rule,"symbol")},{"colour",value(rule,"color")},{"ref","BG" + group.first.substr(2)}};
                auto& ref = relationTags["ref"];
                std::transform(ref.begin(),ref.end(),ref.begin(),[](unsigned char c) { return std::toupper(c); });
                generated += xmlTags(relationTags) + "</relation>\n";
                ++audit.routes[group.first];
            }
        }
        auto closing = static_cast<std::uint64_t>(XML_GetCurrentByteIndex(parser));
        patches.push_back({closing,closing,generated});
        std::ofstream report(reportPath);
        report << "{\n\"pois\":" << audit.pois << ",\n\"synthetic_locality_fallbacks\":" << synthetic
               << ",\n\"ways\":" << ways << ",\n\"route_candidate_ways\":" << candidates
               << ",\n\"route_relations\":" << components << ",\n\"route_components\":" << components
               << ",\n\"largest_component_ways\":" << largest
               << ",\n\"source_types_missing_mapping\":" << jsonCounts(gaps)
               << ",\n\"notes\":[\"Synthetic place=locality on unknown features is PRIVATE-map-only\","
               << "\"Native OSM route relations; no custom phone-side stylesheet\","
               << "\"Display zoom remains controlled by stock OsmAnd, not config zoom_hint\","
               << "\"Route symbol derived from Garmin color; color_only mappings require review\"]\n}\n";
        if (!report) fail("Cannot write native translation report");
    }
    void end(const XML_Char* name) {
        if (transform && depth == 3 && std::string(name) == "tag")
            tagSpans.push_back({tagBegin,static_cast<std::uint64_t>(XML_GetCurrentByteIndex(parser) + XML_GetCurrentByteCount(parser)),""});
        if (transform && depth == 1) generateRelations();
        if (depth == 3 && !firstPass && !auditOnly && std::string(name) == "member" && removeMember) {
            patches.push_back({memberBegin, static_cast<std::uint64_t>(XML_GetCurrentByteIndex(parser) + XML_GetCurrentByteCount(parser)), ""});
            removeMember = false;
        }
        if (depth == 2) {
            if (firstPass) {
                if (attach && kind == "way" && value(tags,"garmin:feature") == "extended_polyline" &&
                    routeTypes.count(value(tags,"garmin:type")) && refs.size() >= 2) {
                    overlayLengths[currentId] = refs.size() - 1;
                    // Repeated edges of one overlay count once in the inverted lookup, as in Python.
                    std::unordered_set<Edge, EdgeHash> distinct;
                    eachEdge(refs, [&](Edge edge) { distinct.insert(edge); });
                    for (const auto& edge : distinct) lookup[edge].push_back(currentId);
                }
                if (kind == "relation" && value(tags,"route") == "hiking" &&
                    value(tags,"bgm:source_feature") == "extended_polyline") hikingRelations.insert(currentId);
            } else {
                if (attach && kind == "way" && tags.count("highway")) {
                    auto order = physical.size();
                    bool matched = false;
                    eachEdge(refs, [&](Edge edge) {
                        auto it = lookup.find(edge);
                        if (it == lookup.end()) return;
                        for (Id overlay : it->second) ++matches[overlay][currentId];
                        matched = true;
                    });
                    if (matched) physical[currentId] = {currentId, refs.empty() ? 0 : refs.size()-1, order};
                }
                if (transform) {
                    if (kind == "node") transformNode();
                    else if (kind == "way") transformWay();
                }
                if (kind == "node") audit.node(tags, lat, lon);
                if (kind == "relation" && value(tags,"route") == "hiking") {
                    ++audit.routes[tags.count("garmin:type") ? value(tags,"garmin:type") : "unknown"];
                    if (hikingRelations.count(currentId)) ++relationCount;
                }
                if (!auditOnly) flush(XML_GetCurrentByteIndex(parser) + XML_GetCurrentByteCount(parser));
            }
        }
        --depth;
    }
    static void onStart(void* data, const XML_Char* name, const XML_Char** attrs) {
        auto self = static_cast<Processor*>(data);
        try { self->start(name, attrs); }
        catch (const std::exception& ex) { self->error = ex.what(); XML_StopParser(self->parser, XML_FALSE); }
    }
    static void onEnd(void* data, const XML_Char* name) {
        auto self = static_cast<Processor*>(data);
        try { self->end(name); }
        catch (const std::exception& ex) { self->error = ex.what(); XML_StopParser(self->parser, XML_FALSE); }
    }
    static void onDoctype(void* data, const XML_Char*, const XML_Char*, const XML_Char*, int) {
        auto self = static_cast<Processor*>(data);
        self->error = "DTD declarations are unsupported in OSM input";
        XML_StopParser(self->parser, XML_FALSE);
    }
    void parse() {
        if (parser) XML_ParserFree(parser);
        parser = XML_ParserCreate(nullptr);
        if (!parser) fail("Cannot create XML parser");
        XML_SetUserData(parser, this);
        XML_SetElementHandler(parser, onStart, onEnd);
        XML_SetStartDoctypeDeclHandler(parser, onDoctype);
        std::ifstream input(inputPath, std::ios::binary);
        if (!input) fail("Cannot read OSM: " + inputPath);
        std::array<char, 1024 * 1024> chunk;
        depth = 0; pendingBase = consumed = 0; pending.clear(); patches.clear(); error.clear();
        while (input) {
            input.read(chunk.data(), chunk.size());
            auto length = input.gcount();
            if (!firstPass && !auditOnly) pending.append(chunk.data(), length);
            if (XML_Parse(parser, chunk.data(), static_cast<int>(length), input.eof()) == XML_STATUS_ERROR) {
                fail(error.empty() ? std::string(XML_ErrorString(XML_GetErrorCode(parser))) +
                    " at XML line " + std::to_string(XML_GetCurrentLineNumber(parser)) : error);
            }
            if (!firstPass && !auditOnly) {
                pending.erase(0, consumed - pendingBase);
                pendingBase = consumed;
            }
        }
        if (!input.eof()) fail("OSM read failed");
        if (!firstPass && !auditOnly) flush(pendingBase + pending.size());
    }
    void run() {
        if (!auditOnly) {
            if (!transform) {
                auto scanStart = std::chrono::steady_clock::now();
                parse();
                std::cerr << "Overlay and relation index scan seconds: " <<
                    std::chrono::duration<double>(std::chrono::steady_clock::now()-scanStart).count() << '\n';
            }
            Id nextId = 1;
            for (Id oldId : invalidRelations) {
                while (occupiedRelations.count(nextId)) ++nextId;
                if (nextId >= relationLimit) fail("No available MapCreator relation IDs below 2^27");
                relationIds[oldId] = nextId;
                occupiedRelations.insert(nextId++);
            }
            output.open(outputPath, std::ios::binary | std::ios::trunc);
            if (!output) fail("Cannot write OSM: " + outputPath);
        }
        firstPass = false;
        auto outputStart = std::chrono::steady_clock::now();
        parse();
        std::cerr << "XML processing and audit pass seconds: " <<
            std::chrono::duration<double>(std::chrono::steady_clock::now()-outputStart).count() << '\n';
        if (!auditOnly) {
            output.close();
            if (!output) fail("Cannot complete OSM output");
        }
        audit.write(auditPath, strict);
        if (!reportPath.empty() && !transform) {
            std::ofstream report(reportPath);
            report << "{\n\"candidate_overlay_ways\":" << overlayLengths.size()
                   << ",\n\"attached_to_existing_highway\":" << replacements.size()
                   << ",\n\"unmatched_overlay_members\":" << overlayLengths.size() - replacements.size()
                   << ",\n\"hiking_relations\":" << relationCount
                   << ",\n\"relation_ids_remapped\":" << relationIds.size()
                   << ",\n\"relation_id_changes\":{";
            bool first = true;
            for (const auto& change : std::map<Id,Id>(relationIds.begin(),relationIds.end())) {
                if (!first) report << ',';
                first = false;
                report << json(std::to_string(change.first)) << ':' << change.second;
            }
            report << "},\n\"policy\":\"Attach only with >=90% shared physical segments and <=10% physical overrun. No invented routable ways.\"\n}\n";
            if (!report) fail("Cannot complete route report");
        }
    }
};
int main(int argc, char** argv) {
    Processor processor;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            auto next = [&]() { if (++i == argc) throw std::runtime_error("Missing value for " + option); return std::string(argv[i]); };
            if (option == "--osm") processor.inputPath = next();
            else if (option == "--out") processor.outputPath = next();
            else if (option == "--report") processor.reportPath = next();
            else if (option == "--audit") processor.auditPath = next();
            else if (option == "--poi-codes") processor.audit.configured = readCodes(next());
            else if (option == "--route-types") processor.routeTypes = readCodes(next());
            else if (option == "--transform-profile") {
                processor.transform = true;
                processor.profile.read(next());
                for (const auto& rule : processor.profile.pois) processor.audit.configured.insert(rule.first);
            }
            else if (option == "--default-width") processor.defaultWidth = std::stoi(next());
            else if (option == "--attach") processor.attach = true;
            else if (option == "--audit-only") processor.auditOnly = true;
            else if (option == "--strict") processor.strict = true;
            else if (option == "--help") {
                std::cout << "native_postprocess --osm INPUT [--out OUTPUT] --poi-codes FILE --audit REPORT [--attach --route-types FILE --report REPORT] [--audit-only] [--strict]\n";
                return 0;
            } else throw std::runtime_error("Unknown option: " + option);
        }
        if (processor.inputPath.empty() || processor.auditPath.empty() || (!processor.auditOnly && processor.outputPath.empty()))
            throw std::runtime_error("--osm, --audit and (unless --audit-only) --out are required");
        if (!processor.auditOnly && std::filesystem::weakly_canonical(processor.inputPath) == std::filesystem::weakly_canonical(processor.outputPath))
            throw std::runtime_error("Input and output must be different paths");
        if (processor.transform && (processor.attach || processor.auditOnly))
            throw std::runtime_error("Translation must run before the attachment pass");
        auto start = std::chrono::steady_clock::now();
        processor.run();
        std::cerr << "Native postprocessing seconds: " << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "native_postprocess: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
