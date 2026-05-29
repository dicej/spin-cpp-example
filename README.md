# Spin C++ Example

This is an example of how to write, build, and run a
[Spin](https://github.com/spinframework/spin) application using C++.  As of this
writing, there's no official C++ SDK for Spin, so the developer experience is
not very polished, but it's manageable with the right tools.

## Prerequisites

- [Spin](https://github.com/spinframework/spin/releases) v4.0.0 or later
    - Add the directory containing the `spin` binary is in your `PATH` environment variable
- [WASI-SDK](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-33) v33 or later
    - Add the `bin` directory to your `PATH` environment variable
- [curl](https://curl.se/download.html) or your favorite browser to test

## Building and Running

The following command will build and run the example.

```
spin build --up
```

While that's running, you can send the app an HTTP request, e.g.:

```
curl -i localhost:3000/hello
```

You can also POST a body and have it echoed back:

```
curl -i -H 'content-type: text/plain' --data-binary @- http://127.0.0.1:3000/echo <<EOF
’Twas brillig, and the slithy toves
      Did gyre and gimble in the wabe:
All mimsy were the borogoves,
      And the mome raths outgrabe.
EOF
```

Finally, you can proxy a request to another server using e.g.:

```
curl -i localhost:3000/proxy?url=https://bytecodealliance.org/
```

## (Optional) Regenerating the bindings

The `bindings` folder in this repository contains C++ bindings generated using
[wit-bindgen](https://github.com/bytecodealliance/wit-bindgen) from the [Spin
WIT interfaces](https://github.com/spinframework/spin/tree/main/wit).  You can
regenerate them yourself if desired by installing `wit-bindgen` v0.58 or later
and running the following (assuming a POSIX-style shell):

```
curl -OL https://github.com/spinframework/spin/archive/refs/tags/v3.4.0.tar.gz
tar xf v3.4.0.tar.gz
rm -r bindings
wit-bindgen cpp spin-3.4.0/wit --out-dir bindings -w http-trigger
```

Note that we use the WIT files from an older Spin v3.4.0 release since
`wit-bindgen-cpp` does not yet support newer WIT features as of this writing.

## (Optional) Update the dependencies

This example relies on [Ada](https://github.com/ada-url/ada) for parsing URLs.
You can update that dependency using:

```
rm -r deps
mkdir deps
for file in ada.h ada.cpp; do \
  (cd deps && curl -OL curl -OL https://github.com/ada-url/ada/releases/download/v3.4.4/$file); \
done 
```

## What about C++ exceptions?

As of this writing, Spin does not yet support the WebAssembly Exception Handling
proposal.  [Wasmtime](https://github.com/bytecodealliance/wasmtime), the
WebAssembly runtime on which Spin is based, has experimental support for that
proposal; once that support has stabilized, Spin will be updated to use it.

Meanwhile, you can make use of the experimental support in Wasmtime by building
and running with the appropriate flags:

```
wasm32-wasip2-clang++ -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -std=c++23 \
  example.cpp bindings/http_trigger.cpp bindings/http_trigger_component_type.o -lunwind \
  -o example.wasm
wasmtime serve --addr 0.0.0.0:3000 -Scli -Wexceptions example.wasm
```


