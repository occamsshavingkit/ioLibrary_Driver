#include "diag_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    diag_command_t command;
    char line[DIAG_LINE_MAX];

    assert(diag_parse_command("help", &command));
    assert(command.kind == DIAG_COMMAND_HELP);
    assert(diag_parse_command("status", &command));
    assert(command.kind == DIAG_COMMAND_STATUS);
    assert(diag_parse_command("list", &command));
    assert(command.kind == DIAG_COMMAND_LIST);
    assert(diag_parse_command("run all", &command));
    assert(command.kind == DIAG_COMMAND_RUN_ALL);
    assert(diag_parse_command("reboot", &command));
    assert(command.kind == DIAG_COMMAND_REBOOT);

    assert(diag_parse_command("run pointer-api", &command));
    assert(command.kind == DIAG_COMMAND_RUN);
    assert(strcmp(command.stage, "pointer-api") == 0);

    assert(diag_parse_command("repeat udp 20", &command));
    assert(command.kind == DIAG_COMMAND_REPEAT);
    assert(command.count == 20u);

    assert(diag_parse_command("net 192.168.2.247 255.255.255.0 192.168.2.1 192.168.2.34 49000", &command));
    assert(command.kind == DIAG_COMMAND_NET);
    assert(command.device_ip[3] == 247u);
    assert(command.host_ip[3] == 34u);
    assert(command.host_port == 49000u);

    assert(diag_parse_command("repeat udp 1", &command));
    assert(diag_parse_command("repeat udp 100000", &command));
    assert(!diag_parse_command("repeat udp 0", &command));
    assert(!diag_parse_command("repeat udp 100001", &command));
    assert(!diag_parse_command("run pointer-api trailing", &command));
    assert(!diag_parse_command("net 256.168.2.247 255.255.255.0 192.168.2.1 192.168.2.34 49000", &command));
    assert(!diag_parse_command("net 192.168.2.247 255.255.255.0 192.168.2.1 192.168.2.34 0", &command));
    assert(!diag_parse_command("net 192.168.2.247 255.255.255.0 192.168.2.1 192.168.2.34 65536", &command));
    assert(!diag_parse_command("unknown", &command));

    assert(diag_format_event(line, sizeof(line), 17u, "pointer-api", "FAIL",
                             "code=readback expected=1234 actual=1200") > 0);
    assert(strcmp(line,
                  "DIAG protocol=1 seq=17 stage=pointer-api event=FAIL code=readback expected=1234 actual=1200\n") == 0);
    assert(diag_format_event(line, sizeof(line), 18u, "udp", "PASS", "") > 0);
    assert(strcmp(line, "DIAG protocol=1 seq=18 stage=udp event=PASS\n") == 0);
    assert(diag_format_event(line, sizeof(line), 19u, "pointer api", "FAIL", "code=name") == -1);
    assert(diag_format_event(line, sizeof(line), 19u, "pointer-api", "BAD EVENT", "code=name") == -1);
    assert(diag_format_event(line, sizeof(line), 19u, "", "FAIL", "code=name") == -1);
    assert(diag_format_event(line, sizeof(line), 19u, "pointer-api", "", "code=name") == -1);
    assert(diag_format_event(line, sizeof(line), 19u, "pointer-api", "FAIL", "code=bad\nline") == -1);
    assert(diag_format_event(line, 8u, 19u, "pointer-api", "FAIL", "code=small") == -1);
    return 0;
}
