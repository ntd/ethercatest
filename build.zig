const std = @import("std");

fn checkSystemLibraryWithPkgConfig(b: *std.Build, name: []const u8) bool {
    const pkg_config_exe = b.graph.env_map.get("PKG_CONFIG") orelse "pkg-config";
    var code: u8 = 0;
    _ = b.runAllowFail(&[_][]const u8{
        pkg_config_exe, "--exists", name
    }, &code, .Ignore) catch return false;

    return code == 0;
}


fn resolveLibraryName(b: *std.Build, comptime fmt: []const u8, args: anytype) ![]u8 {
    const lib = try std.fmt.allocPrint(b.allocator, fmt, args);
    defer b.allocator.free(lib);

    var code: u8 = 0;
    const zig_exe = b.graph.zig_exe;
    const stdout = try b.runAllowFail(&[_][]const u8{
        zig_exe, "cc", "--print-file-name", lib
    }, &code, .Ignore);
    defer b.allocator.free(stdout);

    if (code != 0) {
        return std.Build.RunError.ExitCodeFailure;
    }

    const path = std.mem.trim(u8, stdout, &std.ascii.whitespace);
    return b.allocator.dupe(u8, path);
}

fn checkSystemLibraryWithZigCC(b: *std.Build, name: []const u8) bool {
    // If `--print-file-name` succeeds, `sopath` contains the full path
    // to the library, so it must be waaay longer than `"lib$name.so"`
    const sopath = resolveLibraryName(b, "lib{s}.so", .{ name }) catch return false;
    defer b.allocator.free(sopath);
    if (sopath.len > name.len + 6) {
        return true;
    }

    // Same as above, but with `"lib$name.a"`
    const apath = resolveLibraryName(b, "lib{s}.a", .{ name }) catch return false;
    defer b.allocator.free(apath);
    return apath.len > name.len + 5;
}

/// Returns `true` if the specified library is installed in the host.
/// I did not find a native way of performing such task and, judging
/// from the documentation, it seems this is not the way one is supposed
/// to use the zig build system but... this is not a standard project
/// anyway and I want it to be quickly accessible.
///
/// The implementation is a bit hackish but it should do the job: first
/// it tries with `pkg-config --exists {name}` and then, if that fails,
/// leverages the compiler (`zig cc --print-file-name lib{name}.*`).
fn checkSystemLibrary(b: *std.Build, name: []const u8) bool {
    std.debug.print("Checking for '{s}'... ", .{ name });
    const found = checkSystemLibraryWithPkgConfig(b, name) or
                  checkSystemLibraryWithZigCC(b, name);
    std.debug.print("{s}\n", .{ if (found) "found" else "not found" });
    return found;
}


pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const cflags = if (optimize == .Debug)
        &[_][]const u8{ "-g" }
    else
        &[_][]const u8{ "-O2" };

    if (checkSystemLibrary(b, "libethercat")) {
        const igh = b.addExecutable(.{
            .name = "ethercatest-igh",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
                .link_libc = true,
            }),
        });
        igh.addCSourceFiles(.{
            .files = &[_][]const u8{
                "src/ethercatest-igh.c",
                "src/ethercatest.c",
            },
            .flags = cflags,
        });
        igh.linkSystemLibrary("libethercat");
        b.installArtifact(igh);
    }

    if (checkSystemLibrary(b, "soem")) {
        const soem = b.addExecutable(.{
            .name = "ethercatest-soem",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
                .link_libc = true,
            }),
        });
        soem.addCSourceFiles(.{
            .files = &[_][]const u8{
                "src/ethercatest-soem.c",
                "src/ethercatest.c",
            },
            .flags = cflags,
        });
        soem.linkSystemLibrary("soem");
        b.installArtifact(soem);
    }

    std.debug.print("'gatorcat' is always included\n", .{});
    const gatorcat = b.addExecutable(.{
        .name = "ethercatest-gatorcat",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .root_source_file = b.path("src/ethercatest-gatorcat.zig"),
            .link_libc = true,
        }),
    });
    gatorcat.addIncludePath(b.path("src"));
    gatorcat.addCSourceFile(.{
        .file = b.path("src/ethercatest.c"),
        .flags = cflags,
    });
    const gatorcat_dep = b.dependency("gatorcat", .{
        .target = target,
        .optimize = optimize,
    });
    gatorcat.root_module.addImport("gatorcat", gatorcat_dep.module("gatorcat"));
    b.installArtifact(gatorcat);
}
