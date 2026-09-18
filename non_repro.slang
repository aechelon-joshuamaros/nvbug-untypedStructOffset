struct Flat {
    int4 a;
    int4 b;
    int4 c;
    int4 d;
};

struct Inner {
    int4 a, b;
};

struct Nested {
    Inner a;
    Inner b;
};

struct Output {
    int4 values[8];
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 threadId: SV_DispatchThreadID) {
    ConstantBuffer<Flat> flatBuffer = ResourceDescriptorHeap[0];
    ConstantBuffer<Nested> nestedBuffer = ResourceDescriptorHeap[1];
    RWStructuredBuffer<Output> outputBuffer = ResourceDescriptorHeap[2];
    outputBuffer[0].values[0] = flatBuffer.a;
    outputBuffer[0].values[1] = flatBuffer.b;
    outputBuffer[0].values[2] = flatBuffer.c;
    outputBuffer[0].values[3] = flatBuffer.d;
    outputBuffer[0].values[4] = nestedBuffer.a.a;
    outputBuffer[0].values[5] = nestedBuffer.a.b;
    outputBuffer[0].values[6] = nestedBuffer.b.a;
    outputBuffer[0].values[7] = nestedBuffer.b.b;
}
