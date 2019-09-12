using BinaryProvider # requires BinaryProvider 0.3.0 or later

# Parse some basic command-line arguments
const verbose = "--verbose" in ARGS
const prefix = Prefix(get([a for a in ARGS if a != "--verbose"], 1, joinpath(@__DIR__, "usr")))
products = [
    LibraryProduct(prefix, ["libLLVM"], :libLLVM),
    LibraryProduct(prefix, ["libLTO"], :libLTO),
    LibraryProduct(prefix, ["libclang"], :libclang),
    ExecutableProduct(prefix, "llvm-tblgen", :llvm_tblgen),
    ExecutableProduct(prefix, "clang-tblgen", :clang_tblgen),
    ExecutableProduct(prefix, "llvm-config", :llvm_config),
]

# Download binaries from hosted location
bin_prefix = "https://github.com/staticfloat/LLVMBuilder/releases/download/v6.0.1-7+nowasm"

# Listing of files generated by BinaryBuilder:
download_info = Dict(
    Linux(:aarch64, libc=:glibc, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.aarch64-linux-gnu-gcc4.tar.gz", "258733cf9305f135ab8be9b6c3bea7eb7bcc4e14d951d85e7d9e31a15c255fa5"),
    Linux(:aarch64, libc=:glibc, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.aarch64-linux-gnu-gcc7.tar.gz", "baa5f6ebd814b846d7dc62687962b7a2a4e822c02e5c53030ac958661679e06f"),
    Linux(:aarch64, libc=:glibc, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.aarch64-linux-gnu-gcc8.tar.gz", "a4b63136fc760db96fa7417027c5e1042818c4047b3723dcb2472f495a35d2db"),
    Linux(:armv7l, libc=:glibc, call_abi=:eabihf, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.arm-linux-gnueabihf-gcc4.tar.gz", "1e5733920dfb71ec013634817f8ab51ce24fc62a3bea67838a1818d93cf6b75a"),
    Linux(:armv7l, libc=:glibc, call_abi=:eabihf, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.arm-linux-gnueabihf-gcc7.tar.gz", "d9aad01adc6f3e32f875c85c425440d59f7865648ea7e3b70b1dc1f6d2a4d42c"),
    Linux(:armv7l, libc=:glibc, call_abi=:eabihf, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.arm-linux-gnueabihf-gcc8.tar.gz", "d4c625e28615419456c104681068bc4d28015d23cf6f18fa12744d87ee4ea408"),
    Linux(:i686, libc=:glibc, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.i686-linux-gnu-gcc4.tar.gz", "4430d32b9dbcdf27a91bf9888d169065a459eaae12f0cbe39314e7c5ba7d7251"),
    Linux(:i686, libc=:glibc, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.i686-linux-gnu-gcc7.tar.gz", "ea5c80514a178025458a28fcd442840c7a82cb96e6359cb4fa33e0fe9312f466"),
    Linux(:i686, libc=:glibc, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.i686-linux-gnu-gcc8.tar.gz", "d217b23c5509c7990a79c3c2f46a9c6c2fd2982d9a052e92186db227833c891e"),
    Windows(:i686, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.i686-w64-mingw32-gcc4.tar.gz", "e7045ddbf9da0afee4add08860f98b667a406331250b4f0a998615798d1b44b6"),
    Windows(:i686, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.i686-w64-mingw32-gcc7.tar.gz", "07b519d8ccea686bbb66b770fd58d7878ed818a59a8bef2ed745e3efff1e0d81"),
    Windows(:i686, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.i686-w64-mingw32-gcc8.tar.gz", "99850539331480460e34a631807939c4f4f903da2d4cbbfb41d869c4826acd68"),
    Linux(:powerpc64le, libc=:glibc, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.powerpc64le-linux-gnu-gcc4.tar.gz", "5fca09febf50921d088d0b00da881e37bbe29b1fb80ee3f3dd73baea7ace98fc"),
    Linux(:powerpc64le, libc=:glibc, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.powerpc64le-linux-gnu-gcc7.tar.gz", "2fff9e2baac6f8d1c12c5b92d0fdd729569ced10dedf871604e03c745cd96dab"),
    Linux(:powerpc64le, libc=:glibc, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.powerpc64le-linux-gnu-gcc8.tar.gz", "45bb08eec360606b39a8860bf3385fdba8014261d11fb5a251cdbb473b94156c"),
    MacOS(:x86_64, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-apple-darwin14-gcc4.tar.gz", "688422a003781e2430cb4c1d44a6f07a8e077050b97213695d4995dd8bbff18c"),
    MacOS(:x86_64, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-apple-darwin14-gcc7.tar.gz", "801c50b0bf3a9cebb680257d4bf043c39bab5fb50ec13c1a5c36a4b69b8a6e8c"),
    MacOS(:x86_64, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-apple-darwin14-gcc8.tar.gz", "0b87531d75dd95b1646b8d3c25eaf55f6707af14c6ac9d6571df3a2bcb0b83ca"),
    Linux(:x86_64, libc=:glibc, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-linux-gnu-gcc4.tar.gz", "74af89216a5c3eba22a73b983ff8fa4b6525481368b49fa51e88b22da7cc0dd9"),
    Linux(:x86_64, libc=:glibc, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-linux-gnu-gcc7.tar.gz", "f2c335eb912720a5b3318f909eda1650973043beb033dfb3adc0f7d150b91bf6"),
    Linux(:x86_64, libc=:glibc, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-linux-gnu-gcc8.tar.gz", "33cdab926a924d6befb1bdf7db9598815dcb931c9b1de89a72d129bf5b6fb0eb"),
    Linux(:x86_64, libc=:musl, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-linux-musl-gcc4.tar.gz", "f994ba50dbd5b465911d1d3cb0a3debb7b3c15850b58bb171992a9ec21069bcf"),
    Linux(:x86_64, libc=:musl, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-linux-musl-gcc7.tar.gz", "9030772689d1eed85a41aae4a3413dd2fcff930537dbe782099071cef7276cb4"),
    Linux(:x86_64, libc=:musl, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-linux-musl-gcc8.tar.gz", "62538f3b86629984adccb311ba35120f3fac1ba6cd9265ccda3d722071133834"),
    FreeBSD(:x86_64, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-unknown-freebsd11.1-gcc4.tar.gz", "6baf75dfe38e57024e816d139eb77187c413b255193697d3bd49a769014d2af9"),
    FreeBSD(:x86_64, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-unknown-freebsd11.1-gcc7.tar.gz", "3b38e5c912ed407999f75f6f021569984941034207943bbe393536d2f35f6cba"),
    FreeBSD(:x86_64, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-unknown-freebsd11.1-gcc8.tar.gz", "d5fd71f9ba902128d9c9685454d5dba7d5936ebb0c0e30277141439d97dc16ea"),
    Windows(:x86_64, compiler_abi=CompilerABI(:gcc4)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-w64-mingw32-gcc4.tar.gz", "9214e0e58a3373a8d0c34f55514aa7278d64cc2c9b866a32e3d5ecb3f3b202e5"),
    Windows(:x86_64, compiler_abi=CompilerABI(:gcc7)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-w64-mingw32-gcc7.tar.gz", "74811cb50b41ac40bc69548811063ae595ea5ec8f77108e9a2a2f7c22196376f"),
    Windows(:x86_64, compiler_abi=CompilerABI(:gcc8)) => ("$bin_prefix/LLVM.v6.0.1.x86_64-w64-mingw32-gcc8.tar.gz", "c8baabecbf232437d58848ceeccef31a9ca59d7e452f55ceb26981d17293ba32"),
)

# Install unsatisfied or updated dependencies:
unsatisfied = any(!satisfied(p; verbose=verbose, isolate=true) for p in products)
dl_info = choose_download(download_info, platform_key_abi())
if dl_info === nothing && unsatisfied
    # If we don't have a compatible .tar.gz to download, complain.
    # Alternatively, you could attempt to install from a separate provider,
    # build from source or something even more ambitious here.
    error("Your platform (\"$(Sys.MACHINE)\", parsed as \"$(triplet(platform_key_abi()))\") is not supported by this package!")
end

# If we have a download, and we are unsatisfied (or the version we're
# trying to install is not itself installed) then load it up!
if unsatisfied || !isinstalled(dl_info...; prefix=prefix)
    # Download and install binaries
    install(dl_info...; prefix=prefix, force=true, verbose=verbose)
end

# HACK
if !haskey(ENV, "NO_DEPS")
    # Write out a deps.jl file that will contain mappings for our products
    write_deps_file(joinpath(@__DIR__, "deps.jl"), products, verbose=verbose)
end
