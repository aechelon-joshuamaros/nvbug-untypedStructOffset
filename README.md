## Summary

Reading a nested struct from a buffer retrieved from a descriptor heap produces incorrect results. It appears that the offset of the member in the inner struct is used by itself without considering the offset of the inner struct in the outer struct.

## Machine

- OS: Windows 11 Pro for Workstations, version 10.0.26200 Build 26200
- GPU: NVIDIA RTX A4500

Drivers tested:
- 597.06 (latest studio)
- 616.92 (latest new feature)
- 596.99 (earlier Vulkan beta)
- 596.83 (earlier Vulkan beta)

597.11 (latest Vulkan beta) had abnormal results due to https://github.com/aechelon-joshuamaros/nvbug-bindlessStorageIndexing, which I've reported separately.

## App description

main.cpp is a self-contained application for reproducing this issue. (Disclaimer: this file is mostly AI-generated based on my description of what API calls should be necessary to reproduce the issue I was seeing in a larger application.) The file can be compiled against the base Vulkan SDK and does not link against any dependencies other than the Vulkan loader. The compiled executable should be run from the same folder as the .spv files (which were originally compiled from their corresponding GLSL files.) When run, the application creates two uniform buffers that are each populated with integers 0-15, and a storage buffer for output with room for 32 integers. Descriptors for these are placed into a descriptor heap. A compute shader is dispatched which interprets the first buffer as containing a flat struct, and the second buffer as containing a nested struct:
```glsl
struct Flat { ivec4 a; ivec4 b; ivec4 c; ivec4 d; };

struct Inner { ivec4 a; ivec4 b; };
struct Nested { Inner a; Inner b; };
```
The compute shader copies a, b, c, and d from the first uniform buffer into the first half of the output buffer, and a.a, a.b, b.a, and b.b. from the second uniform buffer into the second half of the output buffer. The contents of the output buffer are then printed to the console.

## Test result

Only the flat struct is copied correctly:
```
Flat struct:
0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 
Nested struct:
0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
```

The behavior is the same across all drivers listed earlier. The output is different on 597.11, but only because [another bug](https://github.com/aechelon-joshuamaros/nvbug-bindlessStorageIndexing) is interfering:
```
Flat struct:
0 1 2 3 0 0 0 0 0 0 0 0 0 0 0 0 
Nested struct:
0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 
```

I also found that in certain cases, a similar Slang shader did not exhibit this issue. This shader is provided in this repo as `non_repro.slang`, and the resulting SPIR-V as `non_repro.spv`. Depending on compiler version, optimization settings, and how exactly the values are accessed, this erratically did or did not exhibit the issue. For the specific SPIR-V uploaded here that does not reproduce the issue, I compiled the shader using Slang 2026.17 with the arguments `-capability spvDescriptorHeapEXT -o non_repro.spv non_repro.slang`
```
Flat struct:
0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 
Nested struct:
0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
```

In the larger application I originally encountered this issue in, I generally saw that data was read from an offset given by the innermost member. For example, with this definition, attempting to read from `outer.*.value` would always read from the start of `outer`, instead of at an appropriate offset depending on `*`:
```glsl
struct Point { vec4 value; }
struct Transform { mat4 value; }
struct Scale { float value; }

struct Outer {
    Point p1, p2;
    Transform t;
    Scale s1, s2, s3;
}
```
