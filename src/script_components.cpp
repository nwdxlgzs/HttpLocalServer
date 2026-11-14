// SPDX-License-Identifier: Apache-2.0
#include "script_components.hpp"
#include "file_type.hpp"
#include "network_features.hpp"
#include "qrcodegen.hpp"
#include "script_wasm.hpp"
#include "sha256.hpp"
#include "storage.hpp"
#include "util.hpp"
#include "version.hpp"
#include "zip_archive.hpp"
#include <sqlite3.h>
#ifdef HLS_ENABLE_TLS
#include <mbedtls/md.h>
#endif
#include <boost/beast/zlib/inflate_stream.hpp>
#include <boost/crc.hpp>
#include <boost/json.hpp>
#include <cstring>
#include <map>
#include <sstream>
namespace hls {
std::string js_text(JSContext *ctx, JSValueConst v) {
    if (!JS_IsString(v))
        throw Failure(400, "Expected a string");
    size_t n = 0;
    auto p = JS_ToCStringLen(ctx, &n, v);
    if (!p)
        throw Failure(400, "Cannot convert text");
    std::string s(p, n);
    JS_FreeCString(ctx, p);
    return s;
}
std::string js_bytes(JSContext *ctx, JSValueConst v, std::size_t maximum) {
    if (JS_IsString(v)) {
        auto s = js_text(ctx, v);
        if (s.size() > maximum)
            throw Failure(413, "Byte input exceeds budget");
        return s;
    }
    JSValue owner = JS_UNDEFINED;
    size_t n = 0, offset = 0, length = 0;
    if (JS_GetTypedArrayType(v) >= 0) {
        owner = JS_GetTypedArrayBuffer(ctx, v, &offset, &length, nullptr);
        if (JS_IsException(owner))
            throw Failure(400, "Detached byte view");
    }
    auto p = JS_GetArrayBuffer(ctx, &n, JS_IsUndefined(owner) ? v : owner);
    if (JS_IsException(owner)) {
        JS_FreeValue(ctx, owner);
        throw Failure(400, "Expected ArrayBuffer or typed array");
    }
    if (!JS_IsUndefined(owner)) {
        if (offset > n || length > n - offset) {
            JS_FreeValue(ctx, owner);
            throw Failure(400, "Invalid byte view");
        }
        n = length;
        if (p)
            p += offset;
    }
    if (!p && n == 0 && !JS_HasException(ctx)) {
        JS_FreeValue(ctx, owner);
        return {};
    }
    if (!p) {
        JS_FreeValue(ctx, owner);
        auto e = JS_GetException(ctx);
        JS_FreeValue(ctx, e);
        throw Failure(400, "Expected non-detached ArrayBuffer or typed array");
    }
    if (n > maximum) {
        JS_FreeValue(ctx, owner);
        throw Failure(413, "Byte input exceeds budget");
    }
    std::string s(reinterpret_cast<char *>(p), n);
    JS_FreeValue(ctx, owner);
    return s;
}
JSValue js_json(JSContext *ctx, const std::string &value) {
    return JS_ParseJSON(ctx, value.data(), value.size(), "native.json");
}
namespace {
JSValue buffer(JSContext *c, const std::string &s) {
    return JS_NewArrayBufferCopy(c, reinterpret_cast<const uint8_t *>(s.data()), s.size());
}
std::string inflate(const std::string &bytes, bool gzip) {
    // Strict single-stream framing; output and expansion ratio stay bounded.
    if (bytes.size() < (gzip ? 18u : 6u))
        throw Failure(400, "Invalid compressed stream");
    size_t offset = gzip ? 10 : 2, trailer = gzip ? 8 : 4;
    if (gzip) {
        const auto flags = static_cast<unsigned char>(bytes[3]);
        if (bytes.compare(0, 3, "\x1f\x8b\x08", 3) || (flags & 0xe0))
            throw Failure(400, "Invalid gzip header");
        auto available = [&](size_t count) {
            if (offset > bytes.size() - trailer || count > bytes.size() - trailer - offset || offset + count > 65536)
                throw Failure(400, "Gzip header exceeds budget");
        };
        if (flags & 4) {
            available(2);
            auto length =
                static_cast<unsigned char>(bytes[offset]) + (static_cast<unsigned char>(bytes[offset + 1]) << 8);
            offset += 2;
            available(length);
            offset += length;
        }
        for (unsigned flag : {8u, 16u})
            if (flags & flag) {
                do {
                    available(1);
                } while (bytes[offset++] != 0);
            }
        if (flags & 2) {
            available(2);
            boost::crc_32_type crc;
            crc.process_bytes(bytes.data(), offset);
            auto expected =
                static_cast<unsigned char>(bytes[offset]) + (static_cast<unsigned char>(bytes[offset + 1]) << 8);
            if ((crc.checksum() & 65535) != expected)
                throw Failure(400, "Gzip header checksum mismatch");
            offset += 2;
        }

    } else {
        auto a = static_cast<unsigned char>(bytes[0]), b = static_cast<unsigned char>(bytes[1]);
        if ((a & 15) != 8 || (a >> 4) > 7 || ((a << 8) | b) % 31 || (b & 32))
            throw Failure(400, "Invalid zlib header");
    }
    boost::beast::zlib::inflate_stream stream;
    boost::beast::zlib::z_params z{};
    z.next_in = bytes.data() + offset;
    z.avail_in = bytes.size() - offset;
    std::string output;
    char chunk[8192];
    for (;;) {
        z.next_out = chunk;
        z.avail_out = sizeof(chunk);
        auto before = z.avail_in;
        boost::system::error_code ec;
        stream.write(z, boost::beast::zlib::Flush::none, ec);
        auto n = sizeof(chunk) - z.avail_out;
        if (output.size() + n > (2u << 20) || output.size() + n > std::max<std::size_t>(4096, bytes.size() * 200))
            throw Failure(413, "Compressed output budget exceeded");
        output.append(chunk, n);
        if (ec == boost::beast::zlib::error::end_of_stream)
            break;
        if (ec || (!n && before == z.avail_in))
            throw Failure(400, "Invalid compressed stream");
    }
    if (z.avail_in != trailer)
        throw Failure(400, "Trailing data in compressed stream");
    const auto *end = reinterpret_cast<const unsigned char *>(bytes.data() + bytes.size() - trailer);
    if (gzip) {
        boost::crc_32_type crc;
        crc.process_bytes(output.data(), output.size());
        uint32_t check = 0, size = 0;
        for (unsigned i = 0; i < 4; ++i) {
            check |= uint32_t(end[i]) << (i * 8);
            size |= uint32_t(end[i + 4]) << (i * 8);
        }
        if (check != crc.checksum() || size != output.size())
            throw Failure(400, "Gzip checksum mismatch");
    } else {
        uint32_t a = 1, b = 0;
        for (unsigned char c : output) {
            a = (a + c) % 65521;
            b = (b + a) % 65521;
        }
        uint32_t expected = 0;
        for (unsigned i = 0; i < 4; ++i)
            expected = (expected << 8) | end[i];
        if (expected != ((b << 16) | a))
            throw Failure(400, "Zlib checksum mismatch");
    }
    return output;
}
} // namespace
struct ScriptComponents::Impl {
    JSContext *ctx;
    Config config;
    Open open;
    Asset asset;
    WasmBridge wasm;
    std::map<unsigned, sqlite3 *> dbs;
    unsigned next = 0;
    bool busy = false;
    std::chrono::steady_clock::time_point until;
    std::unique_ptr<magika::Engine> inference;
    Impl(JSContext *c, const Config &cfg, Open o, Asset a)
        : ctx(c), config(cfg), open(std::move(o)), asset(std::move(a)), wasm(c, cfg, asset) {}
    ~Impl() {
        for (auto &item : dbs)
            sqlite3_close(item.second);
    }
    static int progress(void *opaque) { return std::chrono::steady_clock::now() > static_cast<Impl *>(opaque)->until; }
    static int authorizer(void *, int action, const char *, const char *, const char *, const char *) {
        if (action == SQLITE_ATTACH || action == SQLITE_DETACH || action == SQLITE_PRAGMA)
            return SQLITE_DENY;
        return SQLITE_OK;
    }
    JSValue fn(const char *, int op) {
        auto data = JS_NewBigInt64(ctx, reinterpret_cast<intptr_t>(this));
        auto fn = JS_NewCFunctionData(ctx, call, 1, op, 1, &data);
        JS_FreeValue(ctx, data);
        return fn;
    }
    static JSValue call(JSContext *c, JSValueConst, int argc, JSValueConst *argv, int op, JSValue *data) {
        try {
            int64_t pointer = 0;
            if (JS_ToBigInt64(c, &pointer, data[0]) < 0 || !pointer)
                return JS_ThrowInternalError(c, "Native binding is unavailable");
            auto &self = *reinterpret_cast<Impl *>(static_cast<intptr_t>(pointer));
            if (std::chrono::steady_clock::now() > self.until)
                throw Failure(503, "Script component deadline reached");
            if (self.busy)
                throw Failure(409, "Native component calls cannot re-enter through argument getters");
            struct Active {
                bool &flag;
                explicit Active(bool &f) : flag(f) { flag = true; }
                ~Active() { flag = false; }
            } active(self.busy);
            if (op == 0) {
                if (!argc)
                    throw Failure(400, "Bytes required");
                auto s = js_bytes(c, argv[0]);
                Sha256 hash;
                hash.update(s.data(), s.size());
                return JS_NewString(c, hash.finish().c_str());
            }
            if (op == 1)
                return JS_NewString(c, random_token().c_str());
            if (op >= 2 && op <= 5) {
                if (!argc)
                    throw Failure(400, "Bytes required");
                auto bytes = js_bytes(c, argv[0]);
                return buffer(c, op < 4 ? GzipEncoder(op == 3).write(bytes.data(), bytes.size(), true)
                                        : inflate(bytes, op == 4));
            }
            if (op == 6 || op == 7) {
                if (!argc)
                    throw Failure(400, "Filename required");
                auto name = js_text(c, argv[0]);
                auto type = extension_type(name);
                if (op == 7) {
                    if (!self.open)
                        throw Failure(403, "Filesystem capability unavailable");
                    auto f = self.open(name);
                    if (self.config.magika) {
                        if (!self.inference)
                            self.inference = std::make_unique<magika::Engine>(1, self.config.cpu_kernel);
                        type = detect_type(f, name, *self.inference);
                    } else
                        type = detect_type(f, name);
                }
                return js_json(c, type_json(type, name));
            }
            if (op == 8 || op == 9) {
                if (!argc || !self.open)
                    throw Failure(403, "Filesystem capability unavailable");
                ZipArchive zip(self.open(js_text(c, argv[0])));
                if (op == 8) {
                    boost::json::array items;
                    for (const auto &item : zip.entries())
                        items.emplace_back(boost::json::object{{"path", item.first},
                                                               {"size", item.second.size},
                                                               {"compressed", item.second.compressed},
                                                               {"directory", item.second.directory}});
                    auto text = boost::json::serialize(items);
                    if (text.size() > (2u << 20))
                        throw Failure(413, "ZIP listing exceeds budget");
                    return js_json(c, text);
                }
                if (argc < 2)
                    throw Failure(400, "ZIP entry required");
                auto path = normalize_path(js_text(c, argv[1])).substr(1);
                auto it = zip.entries().find(path);
                if (it == zip.entries().end() || it->second.directory)
                    throw Failure(404, "ZIP entry not found");
                if (it->second.size > (2u << 20))
                    throw Failure(413, "ZIP entry exceeds script budget");
                std::string bytes;
                zip.extract(it->second,
                            [&](const void *p, size_t n) { bytes.append(static_cast<const char *>(p), n); });
                return buffer(c, bytes);
            }
            if (op == 10) {
                if (self.dbs.size() >= 4)
                    throw Failure(503, "At most four in-memory databases");
                sqlite3 *db = nullptr;
                if (sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                    nullptr) != SQLITE_OK) {
                    if (db)
                        sqlite3_close(db);
                    throw Failure(503, "Cannot allocate SQLite database");
                }
                std::unique_ptr<sqlite3, decltype(&sqlite3_close)> pending(db, sqlite3_close);
                if (argc && !JS_IsUndefined(argv[0])) {
                    auto bytes = js_bytes(c, argv[0]);
                    if (bytes.size() < 100 || bytes.compare(0, 16, "SQLite format 3\0", 16) ||
                        static_cast<unsigned char>(bytes[16]) != 16 || bytes[17] != 0)
                        throw Failure(400, "Expected a SQLite snapshot with 4096-byte pages");
                    auto storage = static_cast<unsigned char *>(sqlite3_malloc64(2u << 20));
                    if (!storage)
                        throw Failure(503, "Cannot allocate database snapshot");
                    std::memcpy(storage, bytes.data(), bytes.size());
                    if (sqlite3_deserialize(db, "main", storage, bytes.size(), 2u << 20,
                                            SQLITE_DESERIALIZE_FREEONCLOSE) != SQLITE_OK)
                        throw Failure(400, "Cannot restore SQLite snapshot");
                }
                sqlite3_exec(db, "PRAGMA page_size=4096; PRAGMA max_page_count=512; PRAGMA temp_store=MEMORY;", nullptr,
                             nullptr, nullptr);
                sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 2u << 20);
                sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, 65536);
                sqlite3_limit(db, SQLITE_LIMIT_COLUMN, 128);
                sqlite3_limit(db, SQLITE_LIMIT_EXPR_DEPTH, 64);
                sqlite3_limit(db, SQLITE_LIMIT_VDBE_OP, 20000);
                sqlite3_limit(db, SQLITE_LIMIT_VARIABLE_NUMBER, 128);
                sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
                sqlite3_limit(db, SQLITE_LIMIT_WORKER_THREADS, 0);
                sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
                sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
                sqlite3_set_authorizer(db, authorizer, nullptr);
                sqlite3_progress_handler(db, 1000, progress, &self);
                auto id = ++self.next;
                self.dbs[id] = db;
                pending.release();
                return JS_NewUint32(c, id);
            }
            if (op >= 11 && op <= 14) {
                uint32_t id = 0;
                if (!argc || JS_ToUint32(c, &id, argv[0]))
                    throw Failure(400, "Database handle required");
                auto it = self.dbs.find(id);
                if (it == self.dbs.end())
                    throw Failure(400, "Database is closed");
                auto db = it->second;
                if (op == 14) {
                    sqlite3_close(db);
                    self.dbs.erase(it);
                    return JS_UNDEFINED;
                }
                if (op == 13) {
                    sqlite3_int64 n = 0;
                    // SQLite internally prepares PRAGMA page_count while serializing.
                    // The host operation may do this; application SQL still cannot.
                    sqlite3_set_authorizer(db, nullptr, nullptr);
                    auto p = sqlite3_serialize(db, "main", &n, 0);
                    sqlite3_set_authorizer(db, authorizer, nullptr);
                    if (!p || n > (2u << 20)) {
                        sqlite3_free(p);
                        throw Failure(413, "Database snapshot exceeds budget or is empty");
                    }
                    auto out = JS_NewArrayBufferCopy(c, p, static_cast<size_t>(n));
                    sqlite3_free(p);
                    return out;
                }
                if (argc < 2)
                    throw Failure(400, "SQL required");
                auto sql = js_text(c, argv[1]);
                if (sql.size() > 65536 || sql.find('\0') != std::string::npos)
                    throw Failure(413, "SQL exceeds budget");
                sqlite3_stmt *raw = nullptr;
                const char *tail = nullptr;
                int rc = sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size() + 1), &raw, &tail);
                std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
                if (rc != SQLITE_OK || !raw)
                    throw Failure(400, sqlite3_errmsg(db));
                while (tail && (*tail == ' ' || *tail == '\r' || *tail == '\n' || *tail == '\t'))
                    ++tail;
                if (tail && *tail)
                    throw Failure(400, "Use one SQL statement per call");
                if (argc > 2 && !JS_IsUndefined(argv[2])) {
                    if (!JS_IsArray(argv[2]))
                        throw Failure(400, "SQL parameters must be an array");
                    auto len = JS_GetPropertyStr(c, argv[2], "length");
                    uint32_t count = 0;
                    JS_ToUint32(c, &count, len);
                    JS_FreeValue(c, len);
                    if (count != static_cast<unsigned>(sqlite3_bind_parameter_count(raw)))
                        throw Failure(400, "SQL parameter count mismatch");
                    for (uint32_t i = 0; i < count; ++i) {
                        auto v = JS_GetPropertyUint32(c, argv[2], i);
                        if (JS_IsNull(v))
                            rc = sqlite3_bind_null(raw, i + 1);
                        else if (JS_IsNumber(v)) {
                            double d = 0;
                            JS_ToFloat64(c, &d, v);
                            rc = sqlite3_bind_double(raw, i + 1, d);
                        } else if (JS_IsString(v)) {
                            auto text = js_text(c, v);
                            rc = sqlite3_bind_text(raw, i + 1, text.data(), static_cast<int>(text.size()),
                                                   SQLITE_TRANSIENT);
                        } else {
                            JS_FreeValue(c, v);
                            throw Failure(400, "SQL parameters support null, number and text");
                        }
                        JS_FreeValue(c, v);
                        if (rc != SQLITE_OK)
                            throw Failure(400, sqlite3_errmsg(db));
                    }
                }
                boost::json::array columns, rows;
                for (int i = 0; i < sqlite3_column_count(raw); ++i)
                    columns.emplace_back(sqlite3_column_name(raw, i));
                size_t bytes = 0;
                while ((rc = sqlite3_step(raw)) == SQLITE_ROW) {
                    if (rows.size() >= 1000)
                        throw Failure(413, "SQL result exceeds 1000 rows");
                    boost::json::array row;
                    for (int i = 0; i < sqlite3_column_count(raw); ++i) {
                        auto type = sqlite3_column_type(raw, i);
                        if (type == SQLITE_NULL)
                            row.emplace_back(nullptr);
                        else if (type == SQLITE_INTEGER)
                            row.emplace_back(sqlite3_column_int64(raw, i));
                        else if (type == SQLITE_FLOAT)
                            row.emplace_back(sqlite3_column_double(raw, i));
                        else if (type == SQLITE_TEXT) {
                            auto p = sqlite3_column_text(raw, i);
                            auto n = sqlite3_column_bytes(raw, i);
                            bytes += static_cast<size_t>(n);
                            if (bytes > (2u << 20))
                                throw Failure(413, "SQL output exceeds budget");
                            row.emplace_back(std::string(reinterpret_cast<const char *>(p), n));
                        } else
                            throw Failure(415, "Return blobs as hex(blob) or persist a database snapshot");
                    }
                    rows.emplace_back(std::move(row));
                }
                if (rc != SQLITE_DONE)
                    throw Failure(400, sqlite3_errmsg(db));
                return js_json(
                    c, boost::json::serialize(boost::json::object{{"columns", std::move(columns)},
                                                                  {"rows", std::move(rows)},
                                                                  {"changes", sqlite3_changes(db)},
                                                                  {"lastInsertRowid", sqlite3_last_insert_rowid(db)}}));
            }
            if (op == 15) {
                if (!argc)
                    throw Failure(400, "QR text required");
                auto s = js_text(c, argv[0]);
                if (s.size() > 2048)
                    throw Failure(413, "QR input exceeds budget");
                auto qr = qrcodegen::QrCode::encodeText(s.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
                std::ostringstream out;
                auto size = qr.getSize();
                out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << size + 8 << " " << size + 8
                    << "\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/><path fill=\"black\" d=\"";
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                        if (qr.getModule(x, y))
                            out << "M" << x + 4 << "," << y + 4 << "h1v1h-1z";
                out << "\"/></svg>";
                return JS_NewString(c, out.str().c_str());
            }
            if (op == 16) {
                if (!argc)
                    throw Failure(400, "Text required");
                auto text = js_text(c, argv[0]);
                if (text.size() > (2u << 20))
                    throw Failure(413, "Text exceeds budget");
                return buffer(c, text);
            }
            if (op == 17) {
                if (!argc)
                    throw Failure(400, "Bytes required");
                auto text = js_bytes(c, argv[0]);
                if (!valid_utf8(text))
                    throw Failure(415, "Text decoder requires complete UTF-8");
                return JS_NewStringLen(c, text.data(), text.size());
            }
            if (op == 18) {
                if (argc < 2)
                    throw Failure(400, "Digest requires algorithm and bytes");
                auto algorithm = js_text(c, argv[0]), bytes = js_bytes(c, argv[1]);
                if (algorithm == "SHA256" || algorithm == "sha256") {
                    Sha256 hash;
                    hash.update(bytes.data(), bytes.size());
                    return JS_NewString(c, hash.finish().c_str());
                }
#ifdef HLS_ENABLE_TLS
                if (algorithm != "SHA384" && algorithm != "SHA512")
                    throw Failure(415, "Digest algorithm is not allowed");
                auto md = mbedtls_md_info_from_string(algorithm.c_str());
                if (!md)
                    throw Failure(415, "Digest algorithm is unavailable in this build");
                unsigned char output[64];
                if (mbedtls_md(md, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), output))
                    throw Failure(500, "mbedTLS digest failed");
                static const char hex[] = "0123456789abcdef";
                std::string text;
                for (unsigned i = 0; i < mbedtls_md_get_size(md); ++i) {
                    text += hex[output[i] >> 4];
                    text += hex[output[i] & 15];
                }
                return JS_NewString(c, text.c_str());
#else
                throw Failure(415, "This build provides SHA256 only");
#endif
            }
            throw Failure(400, "Unknown component operation");
        } catch (const std::exception &e) {
            return JS_ThrowTypeError(c, "%s", e.what());
        }
    }
    void install(JSValueConst api) {
        auto group = [&](const char *name, std::initializer_list<std::pair<const char *, int>> ops) {
            auto o = JS_NewObject(ctx);
            for (auto item : ops)
                JS_SetPropertyStr(ctx, o, item.first, fn(item.first, item.second));
            JS_SetPropertyStr(ctx, api, name, o);
        };
        group("text", {{"encode", 16}, {"decode", 17}});
        group("crypto", {{"sha256", 0}, {"randomToken", 1}, {"digest", 18}});
        group("compression", {{"gzip", 2}, {"deflate", 3}, {"gunzip", 4}, {"inflate", 5}});
        group("types", {{"extension", 6}, {"detect", 7}});
        group("zip", {{"list", 8}, {"read", 9}});
        group("sqlite", {{"open", 10}, {"query", 11}, {"execute", 12}, {"snapshot", 13}, {"close", 14}});
        group("qr", {{"svg", 15}});
        boost::json::array args;
        for (const auto &arg : config.workspace_args)
            args.emplace_back(arg);
        JS_SetPropertyStr(ctx, api, "args", js_json(ctx, boost::json::serialize(args)));
        boost::json::object caps{{"server", HLS_VERSION},
                                 {"quickjs", "0.18.0"},
                                 {"wamr", "2.4.5"},
                                 {"wasm", config.wasm},
                                 {"sqlite", sqlite3_libversion()},
                                 {"zip", true},
                                 {"compression", boost::json::array{"gzip", "deflate"}},
                                 {"magika", config.magika},
                                 {"fs", open ? config.script_fs : "off"},
                                 {"qr", true},
                                 {"crypto", boost::json::array{"sha256", "randomToken"}}};
#ifdef HLS_ENABLE_TLS
        caps["mbedTLS"] = "3.6.7";
#else
        caps["mbedTLS"] = false;
#endif
        JS_SetPropertyStr(ctx, api, "capabilities", js_json(ctx, boost::json::serialize(caps)));
        wasm.install(api);
    }
};
ScriptComponents::ScriptComponents(JSContext *c, const Config &cfg, Open o, Asset a)
    : impl_(std::make_unique<Impl>(c, cfg, std::move(o), std::move(a))) {}
ScriptComponents::~ScriptComponents() = default;
void ScriptComponents::install(JSValueConst api) { impl_->install(api); }
void ScriptComponents::deadline(std::chrono::steady_clock::time_point value) {
    impl_->until = value;
    impl_->wasm.deadline(value);
}
} // namespace hls
