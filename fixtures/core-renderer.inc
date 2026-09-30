// SPDX-License-Identifier: GPL-3.0-or-later
// REV5 renderer: two looping PCM voices, distinct channel gains and a stereo device sink.
// ABI sizes come from the pinned audio_core structures, checked by the host test.
static void check_renderer(u32 sm) {
    stage = 7;
    const u32 revision = 0x35564552;
    union { u64 words[9]; u32 fields[18]; } parameters = {0};
    u32* p = parameters.fields;
    p[0] = 48000; p[1] = 240; p[2] = 2; p[4] = 2; p[5] = 1; p[12] = revision;
    const u32 manager = service(sm, "audren:u");
    const u64 work_size = *ipc(manager, 1, 0, 0, 0, 0, parameters.words, 7, 0);
    require(work_size > 0 && work_size <= 0x200000 && !(work_size & 4095));
    Registers heap = set_heap_size(0, 0x400000, 0, 0, 0, 0); check(heap.rc);
    Registers memory = create_transfer(0, heap.first, work_size, 0, 0, 0); check(memory.rc);
    parameters.words[7] = work_size; parameters.words[8] = 0;
    u32 renderer, event;
    ipc_full(manager, 0, 1, 0, 0, 0, parameters.words, 9, &renderer,
             memory.first, 0, 0, 0xffff8001, 0, 0);
    require(*(u32*)ipc(renderer, 0, 0, 0, 0, 0, 0, 0, 0) == 48000);
    require(*(u32*)ipc(renderer, 1, 0, 0, 0, 0, 0, 0, 0) == 240);
    require(*(u32*)ipc(renderer, 2, 0, 0, 0, 0, 0, 0, 0) == 2);
    require(*(u32*)ipc(renderer, 3, 0, 0, 0, 0, 0, 0, 0) == 1);
    ipc(renderer, 7, 0, 0, 0, 0, 0, 0, &event);
    const u64 time_limit[] = {50};
    ipc(renderer, 8, 0, 0, 0, 0, time_limit, 1, 0);
    require(*(u32*)ipc(renderer, 9, 0, 0, 0, 0, 0, 0, 0) == 50);
    PRINT("EDEN_GUEST_RENDERER_INIT_PASS");

    unsigned char* input = (unsigned char*)(heap.first + 0x200000);
    unsigned char* output = input + 0x1000;
    unsigned char* performance = output + 0x1000;
    for (u32 i = 0; i < 0x3000; ++i) input[i] = 0;
    u32* header = (u32*)input;
    header[0] = revision; header[1] = 0x10; header[2] = 0x100;
    header[3] = 0x2e0; header[4] = 0xe0; header[6] = 0x930;
    header[7] = 0x140; header[8] = 0x10; header[15] = 0xf90;
    *(u32*)(input + 0x40) = revision;
    for (u32 index = 0; index < 2; ++index) {
        short* voice_pcm = (short*)(heap.first + 0x210000 + index * 0x1000);
        for (u32 frame = 0; frame < 240; ++frame) voice_pcm[frame] = index == 0 ? (frame % 48 < 24 ? 1024 : -1024) : (frame % 24 < 12 ? 512 : -512);
        u64* pool = (u64*)(input + 0x50 + index * 0x20);
        pool[0] = (u64)voice_pcm; pool[1] = 0x1000; pool[2] = 4; // RequestAttach
        unsigned char* resource = input + 0x150 + index * 0x70;
        ((u32*)resource)[0] = index;
        ((u32*)resource)[1] = index == 0 ? 0x3f000000 : 0x3e800000; // Left gains 0.5, 0.25
        ((u32*)resource)[2] = index == 0 ? 0x3e800000 : 0x3f000000; // Right gains 0.25, 0.5
        resource[0x64] = 1;
        unsigned char* voice = input + 0x230 + index * 0x170;
        ((u32*)voice)[0] = index;
        ((u32*)voice)[0x140 / 4] = index;
        voice[8] = 1; voice[9] = 1; voice[10] = 0; voice[11] = 2; // New, active, started, PCM16
        ((u32*)voice)[3] = 24000; ((u32*)voice)[6] = 1;
        ((u32*)voice)[7] = 0x3f800000; ((u32*)voice)[8] = 0x3f800000;
        ((u32*)voice)[0x3c / 4] = 1;
        ((u32*)voice)[0x5c / 4] = 0xffffffff;
        *(u64*)(voice + 0x60) = (u64)voice_pcm;
        *(u64*)(voice + 0x68) = 240 * sizeof(short);
        *(u32*)(voice + 0x74) = 240; voice[0x78] = 1;
        for (u32 slot = 1; slot < 4; ++slot) voice[0x60 + slot * 0x38 + 0x1a] = 1;
        voice[0x15c] = 0; // Enable 24-to-48 kHz sample-rate conversion.
        voice[0x15e] = 1; // High quality: eight-tap resampling.
    }
    u32* mix = (u32*)(input + 0x40 + 0x10 + 0x100 + 0xe0 + 0x2e0);
    mix[0] = 0x3f800000; mix[1] = 48000; mix[2] = 2; mix[3] = 1;
    mix[0x924 / 4] = 0x7fffffff; mix[0x928 / 4] = 0xffffffff;
    unsigned char* sink = (unsigned char*)mix + 0x930;
    sink[0] = 1; sink[1] = 1;
    const char device[] = "MainAudioOut";
    for (u32 i = 0; i < sizeof(device); ++i) sink[0x20 + i] = device[i];
    *(u32*)(sink + 0x120) = 2; sink[0x124] = 0; sink[0x125] = 1;
    ipc(renderer, 5, 0, 0, 0, 0, 0, 0, 0);
    require(*(u32*)ipc(renderer, 3, 0, 0, 0, 0, 0, 0, 0) == 0);
    u64 previous_frames = 0;
    u64 previous_samples[2] = {0, 0};
    for (u32 update = 0; update < 3; ++update) {
        ipc_full(renderer, 4, 0, 2, input, 0xf90, 0, 0, 0,
                 0, output, 0x1d0, 0, performance, 0x10);
        const u32* response = (const u32*)output;
        require(response[15] == 0x1d0 && response[1] == 0xb0 && response[2] == 0x80);
        require(response[3] == 0x20 && response[7] == 0x20 && response[8] == 0x10);
        require(*(u32*)(output + 0x40 + 0x80 + 0x20 + 0x20 + 0x10 + 0xa0) == 0);
        require(response[10] == 0x10);
        for (u32 index = 0; index < 2; ++index) {
            unsigned char* voice = input + 0x230 + index * 0x170;
            u64* pool = (u64*)(input + 0x50 + index * 0x20);
            if (update == 0) require(*(u32*)(output + 0x40 + index * 0x10) == 5); // Attached
            voice[8] = 0; voice[0x7a] = 1; pool[2] = 5;
            const u64 samples = *(u64*)(output + 0xc0 + index * 0x10);
            require(update == 0 || samples > previous_samples[index]);
            previous_samples[index] = samples;
            print_value(index == 0 ? "EDEN_GUEST_RENDERER_VOICE_SAMPLES=" : "EDEN_GUEST_RENDERER_SECOND_SAMPLES=", samples);
        }
        const u64 frames = *(u64*)(output + 0x1c0);
        require(update == 0 || frames > previous_frames);
        previous_frames = frames;
        print_value("EDEN_GUEST_RENDERER_FRAMES=", frames);
        check(wait(event, 1000000000).rc);
        ONE(sleep_thread, 30000000);
        PRINT("EDEN_GUEST_RENDERER_UPDATE_PASS");
    }
    ipc(renderer, 6, 0, 0, 0, 0, 0, 0, 0);
    require(*(u32*)ipc(renderer, 3, 0, 0, 0, 0, 0, 0, 0) == 1);
    close(event); close(renderer); close(manager); close(memory.first);
    PRINT("EDEN_GUEST_RENDERER_HIGH_SRC_PASS");
    PRINT("EDEN_GUEST_RENDERER_SRC_PASS");
    PRINT("EDEN_GUEST_RENDERER_MIX_PASS");
    PRINT("EDEN_GUEST_RENDERER_VOICE_PASS");
    PRINT("EDEN_GUEST_RENDERER_PASS");
}
