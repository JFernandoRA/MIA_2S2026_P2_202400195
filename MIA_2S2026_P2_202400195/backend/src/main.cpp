#include "httplib.h"
#include "json.hpp"
#include "parser.hpp"
#include "commands.hpp"
#include "ext2_commands.hpp"
#include "account_commands.hpp"
#include "path_commands.hpp"
#include "rep_commands.hpp"
#include "ext3_commands.hpp"
#include "viewer_api.hpp"
#include "file_commands.hpp"
#include "report_registry.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

CmdResult runCommand(const ParsedCommand& cmd) {
    if (cmd.name == "mkdisk")  return cmdMkdisk(cmd);
    if (cmd.name == "rmdisk")  return cmdRmdisk(cmd);
    if (cmd.name == "fdisk")   return cmdFdisk(cmd);
    if (cmd.name == "mount")   return cmdMount(cmd);
    if (cmd.name == "mounted") return cmdMounted(cmd);
    if (cmd.name == "mkfs")    return cmdMkfs(cmd);
    if (cmd.name == "login")   return cmdLogin(cmd);
    if (cmd.name == "logout")  return cmdLogout(cmd);
    if (cmd.name == "mkgrp")   return cmdMkgrp(cmd);
    if (cmd.name == "rmgrp")   return cmdRmgrp(cmd);
    if (cmd.name == "mkusr")   return cmdMkusr(cmd);
    if (cmd.name == "rmusr")   return cmdRmusr(cmd);
    if (cmd.name == "chgrp")   return cmdChgrp(cmd);
    if (cmd.name == "cat")     return cmdCat(cmd);
    if (cmd.name == "mkfile")  return cmdMkfile(cmd);
    if (cmd.name == "mkdir")   return cmdMkdir(cmd);
    if (cmd.name == "rep")     return cmdRep(cmd);
    if (cmd.name == "journaling") return cmdJournaling(cmd);
    if (cmd.name == "unmount") return cmdUnmount(cmd);
    if (cmd.name == "loss")    return cmdLoss(cmd);
    if (cmd.name == "remove")  return cmdRemove(cmd);
    if (cmd.name == "rename")  return cmdRename(cmd);
    if (cmd.name == "copy")    return cmdCopy(cmd);
    if (cmd.name == "move")    return cmdMove(cmd);
    if (cmd.name == "find")    return cmdFind(cmd);
    if (cmd.name == "chown")   return cmdChown(cmd);
    if (cmd.name == "chmod")   return cmdChmod(cmd);

    return {false, "ERROR: comando \"" + cmd.name + "\" no reconocido"};
}

// Ejecuta un comando y, si tuvo éxito y la partición es EXT3, lo registra en el journal
CmdResult dispatch(const ParsedCommand& cmd) {
    CmdResult res = runCommand(cmd);
    if (res.success) {
        journalCommand(cmd);
        updateDiskRegistry(cmd);
        registerReport(cmd);
    }
    return res;
}

struct ScriptResult {
    std::string output;
    int ok = 0;
    int errors = 0;
};

// Procesa un script: varios comandos, uno por línea.
// Los comentarios y las líneas en blanco no cuentan como comando ejecutado.
ScriptResult runScript(const std::string& input) {
    ScriptResult result;
    std::ostringstream output;
    std::istringstream stream(input);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            output << "\n";
            continue;
        }
        if (trimmed[0] == '#') {
            output << trimmed << "\n";
            continue;
        }

        ParsedCommand cmd = parseCommand(trimmed);
        if (!cmd.ok) {
            output << "ERROR: " << cmd.error << "\n";
            result.errors++;
            continue;
        }

        CmdResult res = dispatch(cmd);
        output << res.message << "\n";
        if (res.success) result.ok++; else result.errors++;
    }

    result.output = output.str();
    return result;
}

int main() {
    httplib::Server svr;

    // CORS abierto para que el frontend en React pueda llamar sin lío
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        if (req.method == "OPTIONS") {
            res.status = 200;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ExtreamFS backend corriendo. POST /execute con {\"commands\": \"...\"}", "text/plain");
    });

    svr.Post("/execute", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string commands = body.value("commands", "");
            ScriptResult result = runScript(commands);

            json resp;
            resp["output"] = result.output;
            resp["ok"] = result.ok;
            resp["errors"] = result.errors;
            resp["total"] = result.ok + result.errors;
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            json err;
            err["error"] = std::string("Error procesando la petición: ") + e.what();
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get("/journaling", [](const httplib::Request& req, httplib::Response& res) {
        std::string err;
        json entries = journalToJson(req.get_param_value("id"), err);
        json resp;
        resp["ok"] = err.empty();
        resp["error"] = err;
        resp["entries"] = entries;
        res.set_content(resp.dump(), "application/json");
    });

    auto sendJson = [](httplib::Response& res, const json& j) { res.set_content(j.dump(), "application/json"); };
    auto bodyOf = [](const httplib::Request& req) { try { return json::parse(req.body); } catch (...) { return json::object(); } };

    svr.Post("/login", [&](const httplib::Request& req, httplib::Response& res) {
        json b = bodyOf(req);
        ParsedCommand cmd;
        cmd.name = "login";
        cmd.params["id"] = b.value("id", "");
        cmd.params["user"] = b.value("user", "");
        cmd.params["pass"] = b.value("pass", "");
        CmdResult r = cmdLogin(cmd);
        sendJson(res, {{"ok", r.success}, {"message", r.message}, {"session", sessionJson()}});
    });

    svr.Post("/logout", [&](const httplib::Request&, httplib::Response& res) {
        ParsedCommand cmd;
        cmd.name = "logout";
        CmdResult r = cmdLogout(cmd);
        sendJson(res, {{"ok", r.success}, {"message", r.message}, {"session", sessionJson()}});
    });

    svr.Get("/session", [&](const httplib::Request&, httplib::Response& res) { sendJson(res, sessionJson()); });

    svr.Get("/disks", [&](const httplib::Request&, httplib::Response& res) { sendJson(res, {{"ok", true}, {"disks", disksJson()}}); });

    svr.Get("/ls", [&](const httplib::Request& req, httplib::Response& res) {
        if (!currentSession().active) return sendJson(res, {{"ok", false}, {"error", "debe iniciar sesión"}});
        std::string err;
        json items = listFolderJson(req.get_param_value("id"), req.has_param("path") ? req.get_param_value("path") : "/", err);
        sendJson(res, {{"ok", err.empty()}, {"error", err}, {"items", items}});
    });

    svr.Get("/file", [&](const httplib::Request& req, httplib::Response& res) {
        if (!currentSession().active) return sendJson(res, {{"ok", false}, {"error", "debe iniciar sesión"}});
        std::string err;
        std::string content = readFileJson(req.get_param_value("id"), req.get_param_value("path"), err);
        sendJson(res, {{"ok", err.empty()}, {"error", err}, {"content", content}});
    });

    svr.Get("/bitmaps", [&](const httplib::Request& req, httplib::Response& res) {
        std::string err;
        json bm = bitmapsJson(req.get_param_value("id"), err);
        sendJson(res, {{"ok", err.empty()}, {"error", err}, {"bitmaps", bm}});
    });

    svr.Get("/reports", [&](const httplib::Request&, httplib::Response& res) { sendJson(res, {{"ok", true}, {"reports", reportsJson()}}); });

    svr.Get("/report", [&](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.get_param_value("path");
        std::ifstream f(path, std::ios::binary);
        if (!isRegisteredReport(path) || !f) {
            res.status = 404;
            return res.set_content("reporte no encontrado", "text/plain");
        }
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(data, mimeFor(path));
    });

    std::cout << "ExtreamFS backend escuchando en http://localhost:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);
    return 0;
}