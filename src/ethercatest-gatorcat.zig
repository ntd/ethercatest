const gcat = @import("gatorcat");
const std = @import("std");
const c = @cImport({
    @cInclude("ethercatest.h");
});

pub const std_options = std.Options{
    .log_level = .warn,
};
const info = std.debug.print;

const ArgumentsError = error{
    InterfaceAlreadyDefined,
};

fn usage() void {
    const help =
        \\Usage: ethercatest-gatorcat [INTERFACE] [PERIOD]
        \\  [INTERFACE] Ethernet device to use (e.g. 'eth0')
        \\  [PERIOD]    Scantime in us (0 for roundtrip performances)
        \\
    ;
    info(help, .{});
}

fn getValidInterface() [:0]const u8 {
    return std.mem.span(c.get_default_interface());
}

const Fieldbus = struct {
    allocator: std.mem.Allocator = undefined,
    iface: ?[:0]const u8 = null,
    period: u32 = 5000,
    socket: ?gcat.nic.RawSocket = null,
    port: ?gcat.Port = null,
    eni: ?gcat.Arena(gcat.ENI) = null,
    md: ?gcat.MainDevice = null,
    iteration: u64 = 0,
    iteration_time: i64 = 0,

    pub fn initFromArgs(self: *Fieldbus, allocator: std.mem.Allocator) !bool {
        self.allocator = allocator;
        errdefer self.deinit();

        var args = std.process.args();

        // Skip the first argument (the program name)
        _ = args.next();

        while (args.next()) |arg| {
            if (std.mem.eql(u8, arg, "-h") or std.mem.eql(u8, arg, "--help")) {
                usage();
                self.deinit();
                return false;
            } else if (std.fmt.parseUnsigned(u32, arg, 10)) |period| {
                self.period = period;
            } else |_| {
                if (self.iface) |_| {
                    return ArgumentsError.InterfaceAlreadyDefined;
                } else {
                    // Using the allocator here because I'm not sure
                    // the memory pointed by `arg`
                    // survives to the next `args.next()` call
                    self.iface = try allocator.dupeZ(u8, arg);
                }
            }
        }
        return true;
    }

    pub fn deinit(self: *Fieldbus) void {
        if (self.md) |*md| {
            md.deinit(self.allocator);
            self.md = null;
        }
        if (self.eni) |*eni| {
            eni.deinit();
            self.eni = null;
        }
        if (self.port) |*port| {
            port.deinit();
            self.port = null;
        }
        if (self.socket) |*socket| {
            socket.deinit();
            self.socket = null;
        }
        if (self.iface) |iface| {
            self.allocator.free(iface);
            self.iface = null;
        }
    }

    fn getSocket(self: *Fieldbus) !*gcat.nic.RawSocket {
        if (self.socket == null) {
            const iface = self.iface orelse getValidInterface();
            self.socket = try gcat.nic.RawSocket.init(iface);
            info("gcat.nic.RawSocket.init('{s}') succeeded\n", .{ iface });
        }
        return &self.socket.?;
    }

    fn getPort(self: *Fieldbus) !*gcat.Port {
        if (self.port == null) {
            const socket = try self.getSocket();
            self.port = gcat.Port.init(socket.*.linkLayer(), .{});
            try self.port.?.ping(10_000);
            info("Ping successful\n", .{});
        }
        return &self.port.?;
    }

    fn getScanner(self: *Fieldbus) !gcat.Scanner {
        var scanner = gcat.Scanner.init(try self.getPort(), .{});
        const nsubdevices = try scanner.countSubdevices();
        info("Detected {} subdevices\n", .{ nsubdevices });

        try scanner.busInit(10_000_000, nsubdevices);
        info("scanner.busInit() succeeded\n", .{});

        try scanner.assignStationAddresses(nsubdevices);
        info("scanner.assignStationAddresses() succeeded\n", .{});

        return scanner;
    }

    fn getENI(self: *Fieldbus) !*gcat.Arena(gcat.ENI) {
        if (self.eni == null) {
            var scanner = try self.getScanner();
            self.eni = try scanner.readEni(
                self.allocator,
                10_000_000,
                false,
            );
            info("scanner.readEni() succeeded\n", .{});
        }
        return &self.eni.?;
    }

    fn getMD(self: *Fieldbus) !*gcat.MainDevice {
        if (self.md == null) {
            const eni = try self.getENI();
            self.md = try gcat.MainDevice.init(
                self.allocator,
                try self.getPort(),
                .{ .recv_timeout_us = 20_000, .eeprom_timeout_us = 10_000 },
                eni.*.value,
            );
            info("gcat.MainDevice.init() succeeded\n", .{});
        }
        return &self.md.?;
    }

    pub fn activate(self: *Fieldbus) !void {
        const md = try self.getMD();

        try md.*.busInit(5_000_000);
        info("Switched to INIT\n", .{});

        try md.*.busPreop(10_000_000);
        info("Switched to PREOP\n", .{});

        try md.*.busSafeop(10_000_000);
        info("Switched to SAFE-OP\n", .{});

        try md.*.busOp(10_000_000);
        info("Switched to OP\n", .{});
    }

    fn digital_counter(self: *Fieldbus) void {
        const md = self.getMD() catch unreachable;

        // XXX: not sure how to programmatically map the outputs,
        // so hardcoding my EtherCAT topology here: the second
        // subdevice of my only EtherCAT node is an EL2808
        const el2808 = md.*.subdevices[1];

        // Show a digital counter that updates every 20 iterations
        el2808.runtime_info.pi.outputs[0] = @truncate(self.iteration / 20);
    }

    pub fn iterate(self: *Fieldbus) !void {
        const md = try self.getMD();

        const start = c.get_monotonic_time();
        try md.*.sendRecvCyclicFrames();
        // Skip the cycle when measuring roundtrip time
        if (self.period > 0) {
            self.digital_counter();
        }
        const stop = c.get_monotonic_time();

        self.iteration += 1;
        self.iteration_time = stop - start;
    }

    pub fn dump(self: *const Fieldbus) void {
        info("Iteration {d}: {d} usec\r", .{
            self.iteration,
            self.iteration_time,
        });
    }
};


pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();

    var fieldbus = Fieldbus{};
    if (! try fieldbus.initFromArgs(gpa.allocator())) {
        return;
    }
    defer fieldbus.deinit();

    try fieldbus.activate();

    var min_time: i64 = 0;
    var max_time: i64 = 0;
    const iterations: u64 = 100_000 / (fieldbus.period / 100 + 3);

    while (fieldbus.iteration < iterations) {
        try fieldbus.iterate();
        fieldbus.dump();

        const time = fieldbus.iteration_time;
        if (max_time == 0) {
            min_time = time;
            max_time = time;
        } else if (time < min_time) {
            min_time = time;
        } else if (time > max_time) {
            max_time = time;
        }

        c.wait_next_iteration(time, fieldbus.period);
    }

    info("\nIteration time (usec): min {d}  max {d}\n", .{ min_time, max_time });
}
