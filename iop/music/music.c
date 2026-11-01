// wipeout-ps2's music streamer, an IOP module.
//
// Plays a .spu file (SPU2 ADPCM, made by tools/convert_music.sh) on two
// voices of core 0, reading it as it plays, so the EE does no work for music
// beyond starting tracks and setting the volume (src/sfx_ps2.c).
//
// The file is left and right channel data interleaved in chunks of
// CHUNK_BLOCKS 16-byte blocks; its first block holds the channel count and
// pitch and decodes to near-silence, and it ends with a silent chunk (see
// tools/spu_music/spu_music.c). Each channel has two halves of a buffer in
// sound RAM. The voices play half 0 then half 1 and loop back (the loop flags
// in each block say so), and the SPU2 interrupt fires when the left voice
// reaches the half it isn't playing yet: the stream thread then refills the
// half just played.
//
// Files open through iomanX, so the same code reads host: now and the HDD's
// pfs0: on a console. Uploads use SPU2 DMA channel 1; ps2snd's sample
// uploads use channel 0.

#include "irx_imports.h"

#define MODNAME "wipeout_music"
IRX_ID(MODNAME, 1, 0);

#define MUSIC_RPC_ID 0x57504d55 // also in src/sfx_ps2.c
enum {
	MUSIC_OPEN = 1, // the sound RAM address for the buffers (u32), then the
	                // path, null-terminated; replies with 0 or an error
	MUSIC_CLOSE,
	MUSIC_VOLUME,   // left and right volume, as two u32
	MUSIC_ENDED,    // replies 1 once the track has played to its end
};

#define CHUNK_BLOCKS 1024 // also in tools/spu_music/spu_music.c and src/sfx_ps2.c
#define BLOCK_BYTES 16
#define CHUNK_BYTES (CHUNK_BLOCKS * BLOCK_BYTES)

#define CORE 0
#define VOICE_LEFT 0
#define VOICE_RIGHT 1
#define DMA_CHANNEL 1

// ADPCM block flags
#define BLOCK_LOOP_END    1 // jump to the loop start after this block
#define BLOCK_LOOP_REPEAT 2 // keep playing at a loop end, don't stop
#define BLOCK_LOOP_START  4 // the loop start

static u8 *chunk;            // one channel's chunk from the file
static u32 spu_addr;         // left halves 0 and 1, then right's
static int fd = -1;
static u32 pitch;
static int playing_half;     // the half the voices are playing
static volatile int playing; // voices keyed on, interrupt armed
static volatile int ended;   // the file ran out
static u32 volume_left, volume_right;

static int lock_sema; // the file, the voices and the buffers: RPC and stream threads
static int irq_sema;  // signalled by the SPU2 interrupt

static u32 half_addr(int channel, int half) {
	return spu_addr + (channel * 2 + half) * CHUNK_BYTES;
}

// Read the next chunk of both channels into the given half. Returns 0 at the
// end of the file (or an error).
static int fill_half(int half) {
	for (int channel = 0; channel < 2; channel++) {
		if (read(fd, chunk, CHUNK_BYTES) != CHUNK_BYTES) {
			return 0;
		}
		for (int i = 0; i < CHUNK_BLOCKS; i++) {
			u8 flags = BLOCK_LOOP_REPEAT;
			if (half == 0 && i == 0) {
				flags |= BLOCK_LOOP_START;
			}
			if (half == 1 && i == CHUNK_BLOCKS - 1) {
				flags |= BLOCK_LOOP_END;
			}
			chunk[i * BLOCK_BYTES + 1] = flags;
		}
		sceSdVoiceTrans(DMA_CHANNEL, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, chunk,
			(u32 *)half_addr(channel, half), CHUNK_BYTES);
		sceSdVoiceTransStatus(DMA_CHANNEL, 1);
	}
	return 1;
}

// Interrupt when the left voice starts on the given half. Set after that
// half's upload, which would otherwise trigger it.
static void arm_irq(int half) {
	sceSdSetCoreAttr(CORE | SD_CORE_IRQ_ENABLE, 0);
	sceSdSetAddr(CORE | SD_ADDR_IRQA, half_addr(0, half));
	sceSdSetCoreAttr(CORE | SD_CORE_IRQ_ENABLE, 1);
}

static void set_volume(void) {
	sceSdSetParam(SD_VOICE(CORE, VOICE_LEFT) | SD_VPARAM_VOLL, volume_left);
	sceSdSetParam(SD_VOICE(CORE, VOICE_LEFT) | SD_VPARAM_VOLR, 0);
	sceSdSetParam(SD_VOICE(CORE, VOICE_RIGHT) | SD_VPARAM_VOLL, 0);
	sceSdSetParam(SD_VOICE(CORE, VOICE_RIGHT) | SD_VPARAM_VOLR, volume_right);
}

// With the lock held.
static void stop(void) {
	if (playing) {
		sceSdSetCoreAttr(CORE | SD_CORE_IRQ_ENABLE, 0);
		sceSdSetSwitch(CORE | SD_SWITCH_KOFF, 1 << VOICE_LEFT | 1 << VOICE_RIGHT);
		playing = 0;
	}
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
}

static int irq_handler(int core, void *arg) {
	(void)core;
	(void)arg;
	iSignalSema(irq_sema);
	return 1;
}

// With the lock held.
static int start(const char *path) {
	stop();
	ended = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf(MODNAME ": can't open %s (%d)\n", path, fd);
		return -1;
	}
	u32 header[4];
	if (read(fd, header, sizeof(header)) != sizeof(header) || ((header[1] >> 8) & 0xff) != 2) {
		printf(MODNAME ": %s isn't a stereo .spu file\n", path);
		stop();
		return -2;
	}
	pitch = header[2];
	lseek(fd, 0, SEEK_SET); // the header block plays, as near-silence

	if (!fill_half(0) || !fill_half(1)) {
		stop();
		return -3;
	}
	playing_half = 0;
	for (int channel = 0; channel < 2; channel++) {
		int voice = SD_VOICE(CORE, channel == 0 ? VOICE_LEFT : VOICE_RIGHT);
		sceSdSetParam(voice | SD_VPARAM_PITCH, pitch);
		// Full volume at once, held until key off.
		sceSdSetParam(voice | SD_VPARAM_ADSR1, SD_SET_ADSR1(SD_ADSR_AR_LINEARi, 0, 0, 0xf));
		sceSdSetParam(voice | SD_VPARAM_ADSR2, SD_SET_ADSR2(SD_ADSR_SR_LINEARi, 0x7f, SD_ADSR_RR_LINEARd, 0));
		sceSdSetAddr(voice | SD_VADDR_SSA, half_addr(channel, 0));
	}
	set_volume();
	// libsd's init resets the handler, so set it with every track.
	sceSdSetSpu2IntrHandler(irq_handler, NULL);
	arm_irq(1);
	sceSdSetSwitch(CORE | SD_SWITCH_KON, 1 << VOICE_LEFT | 1 << VOICE_RIGHT);
	playing = 1;
	return 0;
}

static void stream_thread(void *arg) {
	(void)arg;
	while (1) {
		WaitSema(irq_sema);
		WaitSema(lock_sema);
		if (playing) {
			// The voices have moved on to the other half: refill the one
			// they left. The file ends with a silent chunk, so at its end
			// the voices are already playing silence.
			playing_half ^= 1;
			int refill = playing_half ^ 1;
			if (fill_half(refill)) {
				arm_irq(refill);
			}
			else {
				ended = 1;
				stop();
			}
		}
		SignalSema(lock_sema);
	}
}

static u32 rpc_buffer[32] __attribute__((aligned(16)));
static u32 rpc_reply[4] __attribute__((aligned(16)));

static void *rpc_server(int function, void *data, int size) {
	(void)size;
	u32 *args = data;
	rpc_reply[0] = 0;
	switch (function) {
	case MUSIC_OPEN:
		((char *)data)[sizeof(rpc_buffer) - 1] = 0;
		WaitSema(lock_sema);
		spu_addr = args[0];
		rpc_reply[0] = start((const char *)&args[1]);
		SignalSema(lock_sema);
		break;
	case MUSIC_CLOSE:
		WaitSema(lock_sema);
		stop();
		SignalSema(lock_sema);
		break;
	case MUSIC_VOLUME:
		// Not under the lock: it only writes voice registers, and mustn't
		// wait for a file read.
		volume_left = args[0];
		volume_right = args[1];
		set_volume();
		break;
	case MUSIC_ENDED:
		rpc_reply[0] = ended;
		break;
	}
	return rpc_reply;
}

static void rpc_thread(void *arg) {
	static SifRpcDataQueue_t queue;
	static SifRpcServerData_t server;
	(void)arg;
	sceSifInitRpc(0);
	sceSifSetRpcQueue(&queue, GetThreadId());
	sceSifRegisterRpc(&server, MUSIC_RPC_ID, rpc_server, rpc_buffer, NULL, NULL, &queue);
	sceSifRpcLoop(&queue);
}

static int thread(void (*func)(void *), int priority) {
	iop_thread_t t = {
		.attr = TH_C,
		.thread = func,
		.priority = priority,
		.stacksize = 0x800,
	};
	int id = CreateThread(&t);
	if (id >= 0) {
		StartThread(id, NULL);
	}
	return id;
}

int _start(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	iop_sema_t lock = {.initial = 1, .max = 1};
	iop_sema_t irq = {.initial = 0, .max = 1};
	lock_sema = CreateSema(&lock);
	irq_sema = CreateSema(&irq);
	chunk = AllocSysMemory(ALLOC_FIRST, CHUNK_BYTES, NULL);
	if (lock_sema < 0 || irq_sema < 0 || !chunk) {
		printf(MODNAME ": out of IOP memory\n");
		return MODULE_NO_RESIDENT_END;
	}
	// The stream thread runs before the RPC thread, so a refill isn't kept
	// waiting by a call from the EE.
	if (thread(stream_thread, 30) < 0 || thread(rpc_thread, 40) < 0) {
		printf(MODNAME ": can't start its threads\n");
		return MODULE_NO_RESIDENT_END;
	}
	return MODULE_RESIDENT_END;
}
