// ============================================================
// EPUBParser.cpp
// ============================================================

#include "EPUBParser.h"
#include "../storage/FileManager.h"
#include "../../include/config.h"

#include <Arduino.h>
#include <cstring>
#include <cctype>

// ─────────────────────────────────────────────────────────────────────────────
// open
// ─────────────────────────────────────────────────────────────────────────────
bool EPUBParser::open(const char* bookDir) {
    close();

    if (!bookDir || bookDir[0] == '\0') return false;

    _bookDir = String(bookDir);
    // Remove trailing slash if any
    if (_bookDir.endsWith("/") && _bookDir.length() > 1) {
        _bookDir.remove(_bookDir.length() - 1);
    }

    if (!FileManager::instance().exists(_bookDir.c_str())) {
        Serial.printf("[EPUBParser] Directory not found: %s\n", _bookDir.c_str());
        return false;
    }

    String opfPath = _findOpfPath();
    if (opfPath.isEmpty()) {
        Serial.printf("[EPUBParser] No OPF found in %s\n", _bookDir.c_str());
        return false;
    }

    // Store the directory that contains the OPF — needed to resolve relative hrefs
    int lastSlash = opfPath.lastIndexOf('/');
    _opfDir = (lastSlash > 0) ? opfPath.substring(0, lastSlash) : _bookDir;

    if (!_parseOpf(opfPath)) {
        Serial.println(F("[EPUBParser] OPF parse failed"));
        return false;
    }

    _meta.chapterCount = (int)_spineFiles.size();
    _open = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// getMeta / chapterCount
// ─────────────────────────────────────────────────────────────────────────────
BookMeta EPUBParser::getMeta() const { return _meta; }
int      EPUBParser::chapterCount() const { return (int)_spineFiles.size(); }

// ─────────────────────────────────────────────────────────────────────────────
// loadChapter
// ─────────────────────────────────────────────────────────────────────────────
bool EPUBParser::loadChapter(int idx, Chapter& out) {
    if (!_open) return false;
    if (idx < 0 || idx >= (int)_spineFiles.size()) return false;

    const String& path = _spineFiles[idx];
    String raw;
    if (!FileManager::instance().readFile(path.c_str(), raw)) {
        Serial.printf("[EPUBParser] Cannot read chapter file: %s\n", path.c_str());
        return false;
    }

    // Extract title from <title> element if present, else use filename
    String titleTag = _tagContent(raw, "title");
    if (titleTag.isEmpty()) {
        // Use the filename (without directory and extension) as fallback
        int slash = path.lastIndexOf('/');
        int dot   = path.lastIndexOf('.');
        if (dot <= slash) dot = path.length();
        out.title = path.substring(slash + 1, dot);
    } else {
        out.title = titleTag;
        out.title.trim();
    }

    // Strip HTML tags to get plain text
    out.content = _stripTags(raw);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// close
// ─────────────────────────────────────────────────────────────────────────────
void EPUBParser::close() {
    _open = false;
    _bookDir = String();
    _opfDir  = String();
    _meta    = BookMeta();
    _spineFiles.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// _findOpfPath
//
// Strategy:
//   1. Read META-INF/container.xml and extract full-path attribute.
//   2. Fall back: scan the book root directory for any file ending in .opf
//   3. Fall back: try "content.opf" directly in the book root.
// ─────────────────────────────────────────────────────────────────────────────
String EPUBParser::_findOpfPath() const {
    // 1. META-INF/container.xml
    String containerPath = _bookDir + "/META-INF/container.xml";
    String containerXml;
    if (FileManager::instance().readFile(containerPath.c_str(), containerXml)) {
        // Look for: rootfile full-path="..."
        // We search for the full-path attribute inside any <rootfile ...> tag.
        int pos = containerXml.indexOf("rootfile");
        if (pos >= 0) {
            // Find the substring from pos onwards and extract full-path
            String sub = containerXml.substring(pos, pos + 512);
            String fp  = _attrValue(sub, "full-path");
            if (!fp.isEmpty()) {
                return _resolvePath(_bookDir, fp);
            }
        }
    }

    // 2. Scan book root for *.opf
    std::vector<String> rootFiles;
    if (FileManager::instance().listDir(_bookDir.c_str(), rootFiles, ".opf")) {
        if (!rootFiles.empty()) {
            return _bookDir + "/" + rootFiles[0];
        }
    }

    // 3. Hard-coded fallback
    String fallback = _bookDir + "/content.opf";
    if (FileManager::instance().exists(fallback.c_str())) {
        return fallback;
    }

    return String();  // not found
}

// ─────────────────────────────────────────────────────────────────────────────
// _parseOpf
//
// Parses the OPF XML to extract:
//   - dc:title, dc:creator (metadata)
//   - manifest items (id -> href map)
//   - spine itemrefs (idref list → resolved file paths)
//   - cover image path (meta name="cover" or manifest item with id containing "cover")
// ─────────────────────────────────────────────────────────────────────────────
bool EPUBParser::_parseOpf(const String& opfPath) {
    String xml;
    if (!FileManager::instance().readFile(opfPath.c_str(), xml)) {
        return false;
    }

    // ── Metadata ──────────────────────────────────────────────────────────
    {
        String t = _tagContent(xml, "dc:title");
        if (t.isEmpty()) t = _tagContent(xml, "title");
        t.trim();
        _meta.title = t.isEmpty() ? String("Unknown Title") : t;
    }
    {
        String a = _tagContent(xml, "dc:creator");
        if (a.isEmpty()) a = _tagContent(xml, "creator");
        a.trim();
        _meta.author = a.isEmpty() ? String("Unknown Author") : a;
    }

    // ── Build id→href map from <manifest> ─────────────────────────────────
    // Structure: <item id="..." href="..." media-type="..."/>
    // We iterate through <item  occurrences in the manifest section.

    // Locate <manifest> block
    int manStart = xml.indexOf("<manifest");
    int manEnd   = xml.indexOf("</manifest>", manStart);
    if (manStart < 0 || manEnd < 0) {
        Serial.println(F("[EPUBParser] No <manifest> in OPF"));
        return false;
    }
    String manifest = xml.substring(manStart, manEnd);

    // id -> href map (parallel arrays, small enough for heap)
    std::vector<String> itemIds;
    std::vector<String> itemHrefs;
    std::vector<String> itemTypes;

    {
        int searchFrom = 0;
        while (true) {
            int itemPos = manifest.indexOf("<item", searchFrom);
            if (itemPos < 0) break;
            int tagEnd = manifest.indexOf('>', itemPos);
            if (tagEnd < 0) break;
            // Self-closing; extract the attributes
            String tagContent = manifest.substring(itemPos + 5, tagEnd);

            String id   = _attrValue(tagContent, "id");
            String href = _attrValue(tagContent, "href");
            String type = _attrValue(tagContent, "media-type");

            if (!id.isEmpty() && !href.isEmpty()) {
                itemIds.push_back(id);
                itemHrefs.push_back(href);
                itemTypes.push_back(type);
            }
            searchFrom = tagEnd + 1;
        }
    }

    if (itemIds.empty()) {
        Serial.println(F("[EPUBParser] Empty manifest"));
        return false;
    }

    // ── Cover image ───────────────────────────────────────────────────────
    // Method 1: <meta name="cover" content="id"/>
    {
        int metaPos = xml.indexOf("name=\"cover\"");
        if (metaPos < 0) metaPos = xml.indexOf("name='cover'");
        if (metaPos >= 0) {
            // Backtrack to find the opening < of this meta tag
            int tagStart = xml.lastIndexOf('<', metaPos);
            if (tagStart >= 0) {
                int tagClose = xml.indexOf('>', metaPos);
                String metaTag = xml.substring(tagStart + 1, tagClose);
                String coverId = _attrValue(metaTag, "content");
                for (size_t i = 0; i < itemIds.size(); ++i) {
                    if (itemIds[i] == coverId) {
                        _meta.coverPath = _resolvePath(_opfDir, itemHrefs[i]);
                        break;
                    }
                }
            }
        }
    }
    // Method 2: look for an item whose id contains "cover" and is an image
    if (_meta.coverPath.isEmpty()) {
        for (size_t i = 0; i < itemIds.size(); ++i) {
            String id_lc = itemIds[i];
            id_lc.toLowerCase();
            String type_lc = itemTypes[i];
            type_lc.toLowerCase();
            if (id_lc.indexOf("cover") >= 0 &&
                (type_lc.startsWith("image/") || type_lc == "image/jpeg" || type_lc == "image/png")) {
                _meta.coverPath = _resolvePath(_opfDir, itemHrefs[i]);
                break;
            }
        }
    }

    // ── Spine ─────────────────────────────────────────────────────────────
    // Structure: <spine toc="..."> <itemref idref="..."/> ... </spine>
    int spineStart = xml.indexOf("<spine");
    int spineEnd   = xml.indexOf("</spine>", spineStart);
    if (spineStart < 0 || spineEnd < 0) {
        Serial.println(F("[EPUBParser] No <spine> in OPF"));
        return false;
    }
    String spineXml = xml.substring(spineStart, spineEnd);

    _spineFiles.clear();
    {
        int searchFrom = 0;
        while (true) {
            int refPos = spineXml.indexOf("<itemref", searchFrom);
            if (refPos < 0) break;
            int tagEnd = spineXml.indexOf('>', refPos);
            if (tagEnd < 0) break;

            String tagContent = spineXml.substring(refPos + 8, tagEnd);
            String idref      = _attrValue(tagContent, "idref");

            if (!idref.isEmpty()) {
                // Resolve href for this idref
                for (size_t i = 0; i < itemIds.size(); ++i) {
                    if (itemIds[i] == idref) {
                        String absPath = _resolvePath(_opfDir, itemHrefs[i]);
                        _spineFiles.push_back(absPath);
                        break;
                    }
                }
            }
            searchFrom = tagEnd + 1;
        }
    }

    if (_spineFiles.empty()) {
        // Last resort: add all XHTML items from the manifest in document order
        Serial.println(F("[EPUBParser] Spine empty — falling back to manifest XHTML items"));
        for (size_t i = 0; i < itemTypes.size(); ++i) {
            String t = itemTypes[i];
            t.toLowerCase();
            if (t == "application/xhtml+xml" || t == "text/html") {
                _spineFiles.push_back(_resolvePath(_opfDir, itemHrefs[i]));
            }
        }
    }

    return !_spineFiles.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// _attrValue
//
// Given the interior of a tag (everything between < and >), extract the value
// of the named attribute.  Handles both single and double quoted values.
// ─────────────────────────────────────────────────────────────────────────────
String EPUBParser::_attrValue(const String& tagStr, const char* attrName) {
    // Build the search token, e.g. "idref="
    String token(attrName);
    token += '=';

    int pos = tagStr.indexOf(token);
    if (pos < 0) return String();

    int valStart = pos + (int)token.length();
    if (valStart >= (int)tagStr.length()) return String();

    char quote = tagStr[valStart];
    if (quote == '"' || quote == '\'') {
        ++valStart;
        int valEnd = tagStr.indexOf(quote, valStart);
        if (valEnd < 0) valEnd = tagStr.length();
        return tagStr.substring(valStart, valEnd);
    }

    // Unquoted value — read until whitespace or >
    int valEnd = valStart;
    while (valEnd < (int)tagStr.length() &&
           tagStr[valEnd] != ' '  &&
           tagStr[valEnd] != '\t' &&
           tagStr[valEnd] != '\n' &&
           tagStr[valEnd] != '>'  &&
           tagStr[valEnd] != '/') {
        ++valEnd;
    }
    return tagStr.substring(valStart, valEnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// _tagContent
//
// Returns the first text node between <tag ...> and </tag>.
// Only handles simple (non-nested) cases — sufficient for OPF metadata.
// ─────────────────────────────────────────────────────────────────────────────
String EPUBParser::_tagContent(const String& xml, const char* tag) {
    String openTag = String("<") + tag;
    int start = xml.indexOf(openTag);
    if (start < 0) return String();

    // Skip past the closing > of the opening tag
    int gtPos = xml.indexOf('>', start);
    if (gtPos < 0) return String();

    int contentStart = gtPos + 1;

    String closeTag = String("</") + tag + ">";
    int contentEnd = xml.indexOf(closeTag, contentStart);
    if (contentEnd < 0) return String();

    return xml.substring(contentStart, contentEnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// _stripTags
//
// State-machine HTML/XML tag stripper.
//
// States:
//   TEXT   — normal text, emit characters
//   TAG    — inside < ... >, discard characters
//   ENTITY — collecting &...; sequence
//   SCRIPT — inside <script> or <style> block, discard until closing tag
//
// Additional behaviour:
//   - Block-level elements (<p>, <div>, <br>, <h1>–<h6>) emit a newline.
//   - Multiple consecutive whitespace collapsed to single space except
//     for newlines which are preserved.
// ─────────────────────────────────────────────────────────────────────────────
String EPUBParser::_stripTags(const String& src) {
    enum State { TEXT, TAG, ENTITY };

    String out;
    out.reserve(src.length() / 2);  // rough estimate; HTML source is ~2x text

    State   state    = TEXT;
    bool    inScript = false;  // true inside <script> or <style>
    String  tagBuf;
    String  entityBuf;
    bool    lastWasSpace = true;   // suppress leading space

    auto emitChar = [&](char c) {
        if (c == '\n') {
            // Avoid double blank lines
            int len = out.length();
            if (len > 0 && out[len - 1] != '\n') {
                out += '\n';
            }
            lastWasSpace = false;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            if (!lastWasSpace) {
                out += ' ';
                lastWasSpace = true;
            }
        } else {
            out += c;
            lastWasSpace = false;
        }
    };

    auto processTag = [&](const String& tag) {
        // tag is the raw content between < and > (including attributes)
        String lower = tag;
        lower.toLowerCase();
        lower.trim();

        // Check for script/style open tags
        if (lower.startsWith("script") || lower.startsWith("style")) {
            inScript = true;
            return;
        }
        // Check for closing script/style
        if (lower.startsWith("/script") || lower.startsWith("/style")) {
            inScript = false;
            return;
        }

        if (inScript) return;

        // Block-level tags that warrant a newline
        bool isBlock = (lower.startsWith("p")    ||
                        lower.startsWith("/p")   ||
                        lower.startsWith("div")  ||
                        lower.startsWith("/div") ||
                        lower.startsWith("br")   ||
                        lower.startsWith("h1") || lower.startsWith("h2") ||
                        lower.startsWith("h3") || lower.startsWith("h4") ||
                        lower.startsWith("h5") || lower.startsWith("h6") ||
                        lower.startsWith("/h")   ||
                        lower.startsWith("li")   ||
                        lower.startsWith("tr")   ||
                        lower.startsWith("blockquote"));

        if (isBlock) {
            emitChar('\n');
        }
    };

    int len = (int)src.length();
    for (int i = 0; i < len; ++i) {
        char c = src[i];

        switch (state) {

            case TEXT:
                if (c == '<') {
                    tagBuf = String();
                    state  = TAG;
                } else if (c == '&') {
                    entityBuf = String();
                    state     = ENTITY;
                } else {
                    if (!inScript) emitChar(c);
                }
                break;

            case TAG:
                if (c == '>') {
                    processTag(tagBuf);
                    tagBuf = String();
                    state  = TEXT;
                } else {
                    // Accumulate tag content up to a reasonable limit
                    if (tagBuf.length() < 256) {
                        tagBuf += c;
                    }
                }
                break;

            case ENTITY: {
                if (c == ';') {
                    // Decode the entity
                    if (!inScript) {
                        if (entityBuf == "amp")        emitChar('&');
                        else if (entityBuf == "lt")    emitChar('<');
                        else if (entityBuf == "gt")    emitChar('>');
                        else if (entityBuf == "quot")  emitChar('"');
                        else if (entityBuf == "apos")  emitChar('\'');
                        else if (entityBuf == "nbsp")  emitChar(' ');
                        else if (entityBuf == "mdash") { out += "--"; lastWasSpace = false; }
                        else if (entityBuf == "ndash") { out += "-";  lastWasSpace = false; }
                        else if (entityBuf == "hellip"){ out += "..."; lastWasSpace = false; }
                        else if (entityBuf == "lsquo" || entityBuf == "rsquo") emitChar('\'');
                        else if (entityBuf == "ldquo" || entityBuf == "rdquo") emitChar('"');
                        else if (entityBuf.startsWith("#")) {
                            // Numeric entity: &#NNN; or &#xHH;
                            long cp = 0;
                            if (entityBuf[1] == 'x' || entityBuf[1] == 'X') {
                                cp = strtol(entityBuf.c_str() + 2, nullptr, 16);
                            } else {
                                cp = strtol(entityBuf.c_str() + 1, nullptr, 10);
                            }
                            // Encode simple ASCII codepoints; ignore others
                            if (cp >= 32 && cp < 127) {
                                emitChar((char)cp);
                            } else if (cp == 0x2019 || cp == 0x2018) {
                                emitChar('\'');
                            } else if (cp == 0x201C || cp == 0x201D) {
                                emitChar('"');
                            } else if (cp == 0x2014) {
                                out += "--"; lastWasSpace = false;
                            } else if (cp == 0x00A0) {
                                emitChar(' ');
                            }
                            // Otherwise silently skip
                        }
                        // Unknown named entity: silently drop
                    }
                    entityBuf = String();
                    state     = TEXT;
                } else if (entityBuf.length() < 16 && (isalnum((unsigned char)c) || c == '#')) {
                    entityBuf += c;
                } else {
                    // Malformed entity — emit the & and the chars we collected, re-process c
                    if (!inScript) {
                        emitChar('&');
                        for (int j = 0; j < (int)entityBuf.length(); ++j) {
                            emitChar(entityBuf[j]);
                        }
                        // Re-process current character in TEXT state
                        entityBuf = String();
                        state     = TEXT;
                        --i;  // re-process c
                    } else {
                        entityBuf = String();
                        state     = TEXT;
                    }
                }
                break;
            }

            default:
                break;
        }
    }

    // Trim trailing whitespace
    out.trim();
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// _decodeEntities  (standalone helper, not used internally — public utility)
// ─────────────────────────────────────────────────────────────────────────────
String EPUBParser::_decodeEntities(const String& src) {
    // Wrap in fake tags and run through _stripTags with a no-op approach:
    // simpler to just do an in-place replacement for the five standard entities.
    String out;
    out.reserve(src.length());
    int len = (int)src.length();
    for (int i = 0; i < len; ++i) {
        if (src[i] == '&') {
            int semi = src.indexOf(';', i + 1);
            if (semi > 0 && (semi - i) < 16) {
                String entity = src.substring(i + 1, semi);
                if      (entity == "amp")  { out += '&';  i = semi; }
                else if (entity == "lt")   { out += '<';  i = semi; }
                else if (entity == "gt")   { out += '>';  i = semi; }
                else if (entity == "quot") { out += '"';  i = semi; }
                else if (entity == "apos") { out += '\''; i = semi; }
                else if (entity == "nbsp") { out += ' ';  i = semi; }
                else { out += src[i]; }
            } else {
                out += src[i];
            }
        } else {
            out += src[i];
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// _resolvePath
//
// If href is absolute (starts with '/') return bookDir + href.
// Otherwise join opfDir and href, resolving any "../" components.
// ─────────────────────────────────────────────────────────────────────────────
String EPUBParser::_resolvePath(const String& base, const String& href) {
    // URL-encoded paths: skip decoding for simplicity; filenames on SD
    // are expected not to contain special characters.

    if (href.startsWith("/")) {
        // Already absolute within SD root
        return href;
    }

    // Build: base + "/" + href, then normalise ".."
    String combined = base + "/" + href;

    // Normalise by splitting on '/' and processing ".." components
    std::vector<String> parts;
    int start = 0;
    int len   = (int)combined.length();

    for (int i = 0; i <= len; ++i) {
        if (i == len || combined[i] == '/') {
            if (i > start) {
                String seg = combined.substring(start, i);
                if (seg == "..") {
                    if (!parts.empty()) parts.pop_back();
                } else if (seg != ".") {
                    parts.push_back(seg);
                }
            }
            start = i + 1;
        }
    }

    String result;
    for (const String& p : parts) {
        result += '/';
        result += p;
    }
    if (result.isEmpty()) result = "/";
    return result;
}
