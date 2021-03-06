#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/sound.h>
#include <linux/soundcard.h>

#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/semaphore.h>
#include <asm/dma.h>
#include <asm/arch/dma.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-iis.h>
#include <asm/arch/regs-clock.h>
#include <linux/dma-mapping.h>
#include <asm/dma-mapping.h>
#include <asm/arch/hardware.h>
#include <asm/arch/map.h>

#define PFX "s3c2410-uda1341-superlp: "

#define MAX_DMA_CHANNELS 0

/* The S3C2410 has four internal DMA channels. */

#define MAX_S3C2410_DMA_CHANNELS S3C2410_DMA_CHANNELS


#define DMA_BUF_WR 1
#define DMA_BUF_RD 0


#define dma_wrreg(chan, reg, val) __raw_writel((val), (chan)->regs + (reg))


static struct clk *iis_clock;
static void __iomem *iis_base;

static struct s3c2410_dma_client s3c2410iis_dma_out= {
	.name = "I2SSDO",
};

static struct s3c2410_dma_client s3c2410iis_dma_in = {
	.name = "I2SSDI",
};



#ifdef DEBUG
#define DPRINTK printk
#else
#define DPRINTK( x... )
#endif

static void init_s3c2410_iis_bus_rx(void);
static void init_s3c2410_iis_bus_tx(void);

#define DEF_VOLUME 65

/* UDA1341 Register bits */
#define UDA1341_ADDR 0x14  /* uda1341的地址 */

#define UDA1341_REG_DATA0 (UDA1341_ADDR + 0) /* 0x14 ----00010100---0001 01为uda1341地址，00表示DATA0*/  
#define UDA1341_REG_STATUS (UDA1341_ADDR + 2)   /* 0x16 ----0001 0110---0001 01为uda1341地址，10表示STATUS*/

/* status control */
#define STAT0 (0x00)
#define STAT0_RST (1 << 6)
#define STAT0_SC_MASK (3 << 4)
#define STAT0_SC_512FS (0 << 4)
#define STAT0_SC_384FS (1 << 4)
#define STAT0_SC_256FS (2 << 4)
#define STAT0_IF_MASK (7 << 1)
#define STAT0_IF_I2S (0 << 1)
#define STAT0_IF_LSB16 (1 << 1)
#define STAT0_IF_LSB18 (2 << 1)
#define STAT0_IF_LSB20 (3 << 1)
#define STAT0_IF_MSB (4 << 1)
#define STAT0_IF_LSB16MSB (5 << 1)
#define STAT0_IF_LSB18MSB (6 << 1)
#define STAT0_IF_LSB20MSB (7 << 1)
#define STAT0_DC_FILTER (1 << 0)
#define STAT0_DC_NO_FILTER (0 << 0)

#define STAT1 (0x80)
#define STAT1_DAC_GAIN (1 << 6) /* gain of DAC */
#define STAT1_ADC_GAIN (1 << 5) /* gain of ADC */
#define STAT1_ADC_POL (1 << 4) /* polarity of ADC */
#define STAT1_DAC_POL (1 << 3) /* polarity of DAC */
#define STAT1_DBL_SPD (1 << 2) /* double speed playback */
#define STAT1_ADC_ON (1 << 1) /* ADC powered */
#define STAT1_DAC_ON (1 << 0) /* DAC powered */

/* data0 direct control */
#define DATA0 (0x00)
#define DATA0_VOLUME_MASK (0x3f)
#define DATA0_VOLUME(x) (x)

#define DATA1 (0x40)
#define DATA1_BASS(x) ((x) << 2)
#define DATA1_BASS_MASK (15 << 2)
#define DATA1_TREBLE(x) ((x))
#define DATA1_TREBLE_MASK (3)

#define DATA2 (0x80)
#define DATA2_PEAKAFTER (0x1 << 5)
#define DATA2_DEEMP_NONE (0x0 << 3)
#define DATA2_DEEMP_32KHz (0x1 << 3)
#define DATA2_DEEMP_44KHz (0x2 << 3)
#define DATA2_DEEMP_48KHz (0x3 << 3)
#define DATA2_MUTE (0x1 << 2)
#define DATA2_FILTER_FLAT (0x0 << 0)
#define DATA2_FILTER_MIN (0x1 << 0)
#define DATA2_FILTER_MAX (0x3 << 0)
/* data0 extend control */
#define EXTADDR(n) (0xc0 | (n))
#define EXTDATA(d) (0xe0 | (d))

#define EXT0 0
#define EXT0_CH1_GAIN(x) (x)
#define EXT1 1
#define EXT1_CH2_GAIN(x) (x)
#define EXT2 2
#define EXT2_MIC_GAIN_MASK (7 << 2)
#define EXT2_MIC_GAIN(x) ((x) << 2)
#define EXT2_MIXMODE_DOUBLEDIFF (0)
#define EXT2_MIXMODE_CH1 (1)
#define EXT2_MIXMODE_CH2 (2)
#define EXT2_MIXMODE_MIX (3)
#define EXT4 4
#define EXT4_AGC_ENABLE (1 << 4)
#define EXT4_INPUT_GAIN_MASK (3)
#define EXT4_INPUT_GAIN(x) ((x) & 3)
#define EXT5 5
#define EXT5_INPUT_GAIN(x) ((x) >> 2)
#define EXT6 6
#define EXT6_AGC_CONSTANT_MASK (7 << 2)
#define EXT6_AGC_CONSTANT(x) ((x) << 2)
#define EXT6_AGC_LEVEL_MASK (3)
#define EXT6_AGC_LEVEL(x) (x)

#define AUDIO_NAME "UDA1341"
#define AUDIO_NAME_VERBOSE "UDA1341 audio driver"

#define AUDIO_FMT_MASK (AFMT_S16_LE)
#define AUDIO_FMT_DEFAULT (AFMT_S16_LE)  /* 默认的采样格式为 小端模式,有符号16位*/

#define AUDIO_CHANNELS_DEFAULT 2   /* 默认通道 */
#define AUDIO_RATE_DEFAULT 44100   /* 默认的音频采样频率为44.1KHz */

#define AUDIO_NBFRAGS_DEFAULT 8    /* 片段数 */
#define AUDIO_FRAGSIZE_DEFAULT 8192  /* 片段大小为8K */

#define S_CLOCK_FREQ 384
#define PCM_ABS(a) (a < 0 ? -a : a)

#define IISPSR_A(x)  (x<<5)
#define IISPSR_B(x)  (x<<0)

typedef struct {  /* 音频缓冲区 */
	int size; /* buffer size */  /* 缓冲区大小 */
	char *start; /* point to actual buffer */  /* 缓冲区起始地址 */
	dma_addr_t dma_addr; /* physical buffer address */  /* 缓冲区物理地址 */
	struct semaphore sem; /* down before touching the buffer */  
	int master; /* owner for buffer allocation, contain size when true */ /* 缓冲区片段大小*/
} audio_buf_t;

typedef struct {  /* 音频流 */
	audio_buf_t *buffers; /* pointer to audio buffer structures */  /* 指向缓冲区 */
	audio_buf_t *buf; /* current buffer used by read/write */ /* 读写缓冲区 */
	u_int buf_idx; /* index for the pointer above */  /* 缓冲区引索 */
	u_int fragsize; /* fragment i.e. buffer size */ /* 片段大小 =4K */
	u_int nbfrags; /* nbr of fragments */   /* 片段数 =8 */
	unsigned char  dma_ch; /* DMA channel (channel2 for audio) */  /* DMA通道 */
	u_int dma_ok;  /* DMA是否准备好 */
} audio_stream_t;

static audio_stream_t output_stream;/* 输出流 */ 
static audio_stream_t input_stream; /* input */  /* 输入流 */

#define NEXT_BUF(_s_,_b_) { \
(_s_)->_b_##_idx++; \
(_s_)->_b_##_idx %= (_s_)->nbfrags; \
(_s_)->_b_ = (_s_)->buffers + (_s_)->_b_##_idx; }


static u_int audio_rate;  /* 音频速率为 = 默认的音频采样频率为44.1KHz */ 
static int audio_channels; /* 音频通道 =2 */
static int audio_fmt;    /* 音频格式 =采样格式为 小端模式,有符号16� */
static u_int audio_fragsize;  /* 片段大小 =4K*/
static u_int audio_nbfrags;   /* 片段数目 =8*/


static int audio_rd_refcount;  /* 读计数 */
static int audio_wr_refcount;  /* 写计数 */
#define audio_active (audio_rd_refcount | audio_wr_refcount)

static int audio_dev_dsp;  /* DSP设备 */
static int audio_dev_mixer;  /* mixer设备 */
static int audio_mix_modcnt;

static int uda1341_volume;  /* 音量 */
//static u8 uda_sampling;
static int uda1341_boost;  /* 增加 */
static int mixer_igain=0x4; /* 混音器增益-6db*/

/* 地址模式 */
static void uda1341_l3_address(u8 data)  /* data:  bit[7:2]:设备地址,uda1341的地址为000101;  bit[1:0]:00-->DATA0---直接地址寄存器;扩展地址寄存器
                                                                                                                                                                                    01-->DATA1---高电平值读出(信息从uda1341到微处理器)
                                                                                                                                                                                    10--->STATUS---复位等*/
{
	int i;
	unsigned long flags;

	local_irq_save(flags);

// write_gpio_bit(GPIO_L3MODE, 0);
       /* 地址模式: L3MODE=0;L3CLOCK连续8个脉冲;随后是8为的数据 */
	s3c2410_gpio_setpin(S3C2410_GPB2,0);  /* L3MODE=0 */
// write_gpio_bit(GPIO_L3CLOCK, 1);
	s3c2410_gpio_setpin(S3C2410_GPB4,1);  /* L3CLOCK=1 */
	udelay(1);
       /* 发送八位的数据 */
	for (i = 0; i < 8; i++) {
		if (data & 0x1) {
			s3c2410_gpio_setpin(S3C2410_GPB4,0);
			s3c2410_gpio_setpin(S3C2410_GPB3,1);
			udelay(1);
			s3c2410_gpio_setpin(S3C2410_GPB4,1);
		} else {
			s3c2410_gpio_setpin(S3C2410_GPB4,0);
			s3c2410_gpio_setpin(S3C2410_GPB3,0);
			udelay(1);
			s3c2410_gpio_setpin(S3C2410_GPB4,1);
		}
		data >>= 1;
	}

	s3c2410_gpio_setpin(S3C2410_GPB2,1);
	s3c2410_gpio_setpin(S3C2410_GPB4,1);
	local_irq_restore(flags);
}

static void uda1341_l3_data(u8 data)  /* 写数据 */
{
	int i;
	unsigned long flags;

	local_irq_save(flags);
	udelay(1);

	for (i = 0; i < 8; i++) {
		if (data & 0x1) {
			s3c2410_gpio_setpin(S3C2410_GPB4,0);
			s3c2410_gpio_setpin(S3C2410_GPB3,1);
			udelay(1);
			s3c2410_gpio_setpin(S3C2410_GPB4,1);
		} else {
			s3c2410_gpio_setpin(S3C2410_GPB4,0);
			s3c2410_gpio_setpin(S3C2410_GPB3,0);
			udelay(1);
			s3c2410_gpio_setpin(S3C2410_GPB4,1);
		}

		data >>= 1;
	}

	local_irq_restore(flags);
}

/* 清空缓冲区 */
static void audio_clear_buf(audio_stream_t * s)
{
	DPRINTK("audio_clear_buf\n");

	if(s->dma_ok) s3c2410_dma_ctrl(s->dma_ch, S3C2410_DMAOP_FLUSH);  /* 冲洗DMA */

	if (s->buffers) {
		int frag;

		for (frag = 0; frag < s->nbfrags; frag++) {
			if (!s->buffers[frag].master)
				continue;
			dma_free_coherent(NULL,
					  s->buffers[frag].master,
					  s->buffers[frag].start,
					  s->buffers[frag].dma_addr); /* 释放由dma_alloc_coherent()函数建立的一致性缓存 */
		}
		kfree(s->buffers); /* 是否内存 */
		s->buffers = NULL;
	}

	s->buf_idx = 0;
	s->buf = NULL;
}

/* 设置缓冲区 */
static int audio_setup_buf(audio_stream_t * s)
{
	int frag;
	int dmasize = 0;
	char *dmabuf = 0;
	dma_addr_t dmaphys = 0;

	if (s->buffers)
		return -EBUSY;

	s->nbfrags = audio_nbfrags; /* =8 */
	s->fragsize = audio_fragsize;/* =4K*/

	s->buffers = (audio_buf_t *)
			kmalloc(sizeof(audio_buf_t) * s->nbfrags, GFP_KERNEL);  /* 分配 sizeof(audio_buf_t) * s->nbfrags大小的内存*/
	if (!s->buffers)
		goto err;
	memset(s->buffers, 0, sizeof(audio_buf_t) * s->nbfrags);

	for (frag = 0; frag < s->nbfrags; frag++) {  /* for (frag = 0; frag < 8; frag++) */
		audio_buf_t *b = &s->buffers[frag];  /* 获取每个缓冲区的地址 */

		if (!dmasize) {/* dmasize =0*/
			dmasize = (s->nbfrags - frag) * s->fragsize;  
			do {
				dmabuf = dma_alloc_coherent(NULL, dmasize, &dmaphys, GFP_KERNEL|GFP_DMA); /* 申请DMA缓冲区，建立一致性映射 */
				if (!dmabuf)
					dmasize -= s->fragsize;
			} while (!dmabuf && dmasize);
			if (!dmabuf)
				goto err;
			b->master = dmasize;  /* 保存分配的内存空间大小 */
		}

		b->start = dmabuf;
		b->dma_addr = dmaphys;
		init_MUTEX(&b->sem);
		DPRINTK("buf %d: start %p dma %d\n", frag, b->start, b->dma_addr);

		dmabuf += s->fragsize;
		dmaphys += s->fragsize;
		dmasize -= s->fragsize;
	}
	
       /* 指向第一个缓冲区 */
	s->buf_idx = 0;
	s->buf = &s->buffers[s->buf_idx];
	return 0;

err:
		printk(AUDIO_NAME ": unable to allocate audio memory\n ");
audio_clear_buf(s);
return -ENOMEM;
}

 /* 设置DMA缓冲区完成回调函数 */
static void audio_dmaout_done_callback(struct s3c2410_dma_chan *ch, void *buf, int size,
				       enum s3c2410_dma_buffresult result)
{
	audio_buf_t *b = (audio_buf_t *) buf;
	up(&b->sem);
	wake_up(&b->sem.wait);  /* 唤醒睡眠在poll设置的等待队列上的进程 */
}

static void audio_dmain_done_callback(struct s3c2410_dma_chan *ch, void *buf, int size,
				      enum s3c2410_dma_buffresult result)
{
	audio_buf_t *b = (audio_buf_t *) buf;
	b->size = size;
	up(&b->sem);
	wake_up(&b->sem.wait);
}

/* using when write */
 /*同步音频 */
static int audio_sync(struct file *file)
{
	audio_stream_t *s = &output_stream;
	audio_buf_t *b = s->buf;

	DPRINTK("audio_sync\n");

	if (!s->buffers)
		return 0;

	if (b->size != 0) {
		down(&b->sem);
		s3c2410_dma_enqueue(s->dma_ch, (void *) b, b->dma_addr, b->size); /* 对每个缓冲区进行排队，等待处理 */
		b->size = 0;
		NEXT_BUF(s, buf);
	}

	b = s->buffers + ((s->nbfrags + s->buf_idx - 1) % s->nbfrags);
	if (down_interruptible(&b->sem))
		return -EINTR;
	up(&b->sem);

	return 0;
}

static inline int copy_from_user_mono_stereo(char *to, const char *from, int count)
{
	u_int *dst = (u_int *)to;
	const char *end = from + count;

	if (access_ok(VERIFY_READ, from, count))
		return -EFAULT;

	if ((int)from & 0x2) {
		u_int v;
		__get_user(v, (const u_short *)from); from += 2;
		*dst++ = v | (v << 16);
	}

	while (from < end-2) {
		u_int v, x, y;
		__get_user(v, (const u_int *)from); from += 4;
		x = v << 16;
		x |= x >> 16;
		y = v >> 16;
		y |= y << 16;
		*dst++ = x;
		*dst++ = y;
	}

	if (from < end) {
		u_int v;
		__get_user(v, (const u_short *)from);
		*dst = v | (v << 16);
	}

	return 0;
}

/* 播放 */
static ssize_t smdk2410_audio_write(struct file *file, const char *buffer,
				    size_t count, loff_t * ppos)
{
	const char *buffer0 = buffer;
	audio_stream_t *s = &output_stream;
	int chunksize, ret = 0;

	DPRINTK("audio_write : start count=%d\n", count);

	switch (file->f_flags & O_ACCMODE) {
		case O_WRONLY:
		case O_RDWR:
			break;
		default:
			return -EPERM;
	}

	if (!s->buffers && audio_setup_buf(s)) /* 设置缓冲区 */
		return -ENOMEM;

	count &= ~0x03;

	while (count > 0) {
		audio_buf_t *b = s->buf; /* 获取缓冲区首地址 */

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			if (down_trylock(&b->sem))
				break;
		} else {
			ret = -ERESTARTSYS;
			if (down_interruptible(&b->sem))
				break;
		}

		if (audio_channels == 2) {  /*  audio_channels = 2*/
			chunksize = s->fragsize - b->size; /* 块大小 */
			if (chunksize > count)
				chunksize = count;
			DPRINTK("write %d to %d\n", chunksize, s->buf_idx);
			if (copy_from_user(b->start + b->size, buffer, chunksize)) {  /* 将用户数据写入缓冲区 */
				up(&b->sem);
				return -EFAULT;
			}
			b->size += chunksize;
		} else {
			chunksize = (s->fragsize - b->size) >> 1;

			if (chunksize > count)
				chunksize = count;
			DPRINTK("write %d to %d\n", chunksize*2, s->buf_idx);
			if (copy_from_user_mono_stereo(b->start + b->size,
			    buffer, chunksize)) {
				    up(&b->sem);
				    return -EFAULT;
			    }

			    b->size += chunksize*2;
		}

		buffer += chunksize;
		count -= chunksize;
		if (b->size < s->fragsize) {
			up(&b->sem);
			break;
		}

		if((ret = s3c2410_dma_enqueue(s->dma_ch, (void *) b, b->dma_addr, b->size))) { /* 对缓冲区重新排序 并启动DMA传输*/
			printk(PFX"dma enqueue failed.\n");
			return ret;
		}
		b->size = 0;
		NEXT_BUF(s, buf);  /*指向下一个缓冲区 */
	}

	if ((buffer - buffer0))
		ret = buffer - buffer0;

	DPRINTK("audio_write : end count=%d\n\n", ret);

	return ret;
}

/* 录音 */
static ssize_t smdk2410_audio_read(struct file *file, char *buffer,
				   size_t count, loff_t * ppos)
{
	const char *buffer0 = buffer;
	audio_stream_t *s = &input_stream;
	int chunksize, ret = 0;

	DPRINTK("audio_read: count=%d\n", count);
/*
	if (ppos != &file->f_pos)
	return -ESPIPE;
*/
	if (!s->buffers) {
		int i;

		if (audio_setup_buf(s))
			return -ENOMEM;

		for (i = 0; i < s->nbfrags; i++) {
			audio_buf_t *b = s->buf;
			down(&b->sem);
			s3c2410_dma_enqueue(s->dma_ch, (void *) b, b->dma_addr, s->fragsize); /* 对每个DMA缓冲区进行排队并且启动DMA传输---
			                                                                                                                         即将录音数据拷贝到缓冲区*/
			NEXT_BUF(s, buf);
		}
	}

	while (count > 0) {
		audio_buf_t *b = s->buf;

		/* Wait for a buffer to become full */
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			if (down_trylock(&b->sem))
				break;
		} else {
			ret = -ERESTARTSYS;
			if (down_interruptible(&b->sem))
				break;
		}

		chunksize = b->size;
		if (chunksize > count)
			chunksize = count;
		DPRINTK("read %d from %d\n", chunksize, s->buf_idx);
		if (copy_to_user(buffer, b->start + s->fragsize - b->size,  /* 将数据拷贝到用户空间 */
		    chunksize)) {
			    up(&b->sem);
			    return -EFAULT;
		    }

		    b->size -= chunksize;

		    buffer += chunksize;
		    count -= chunksize;
		    if (b->size > 0) {
			    up(&b->sem);
			    break;
		    }

		    /* Make current buffer available for DMA again */
		    s3c2410_dma_enqueue(s->dma_ch, (void *) b, b->dma_addr, s->fragsize); /* 重新拷贝录音数据 */

		    NEXT_BUF(s, buf);
	}

	if ((buffer - buffer0))
		ret = buffer - buffer0;

	// DPRINTK("audio_read: return=%d\n", ret);

	return ret;
}


static unsigned int smdk2410_audio_poll(struct file *file,
					struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	int i;

	DPRINTK("audio_poll(): mode=%s\n",
		(file->f_mode & FMODE_WRITE) ? "w" : "");

	if (file->f_mode & FMODE_READ) { /* 读 */
		if (!input_stream.buffers && audio_setup_buf(&input_stream))
			return -ENOMEM;
		poll_wait(file, &input_stream.buf->sem.wait, wait);

		for (i = 0; i < input_stream.nbfrags; i++) {
			if (atomic_read(&input_stream.buffers[i].sem.count) > 0) /* 说明持有信号量 */
				mask |= POLLIN | POLLWRNORM;  /* 有数据 */
			break;
		}
	}


	if (file->f_mode & FMODE_WRITE) { /* 写 */
		if (!output_stream.buffers && audio_setup_buf(&output_stream)) /* 检查缓冲区是否存在 */
			return -ENOMEM;
		poll_wait(file, &output_stream.buf->sem.wait, wait);

		for (i = 0; i < output_stream.nbfrags; i++) {
			if (atomic_read(&output_stream.buffers[i].sem.count) > 0) /* 说明持有信号量 */
				mask |= POLLOUT | POLLWRNORM; /* 可写 */
			break;
		}
	}

	DPRINTK("audio_poll() returned mask of %s\n",
		(mask & POLLOUT) ? "w" : "");

	return mask;
}


static loff_t smdk2410_audio_llseek(struct file *file, loff_t offset,
				    int origin)
{
	return -ESPIPE;
}


static int smdk2410_mixer_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int ret;
	long val = 0;

	switch (cmd) {
		case SOUND_MIXER_INFO: /* 获取mixer信息 */
		{
			mixer_info info;
			strncpy(info.id, "UDA1341", sizeof(info.id));
			strncpy(info.name,"Philips UDA1341", sizeof(info.name));
			info.modify_counter = audio_mix_modcnt;
			return copy_to_user((void *)arg, &info, sizeof(info)); /* 将信息拷贝到用户空间 */
		}

		case SOUND_OLD_MIXER_INFO:  /* 获取旧的mixer信息 */
		{
			_old_mixer_info info;
			strncpy(info.id, "UDA1341", sizeof(info.id));
			strncpy(info.name,"Philips UDA1341", sizeof(info.name));
			return copy_to_user((void *)arg, &info, sizeof(info));
		}

		case SOUND_MIXER_READ_STEREODEVS: /* 读立体声设备 */
			return put_user(0, (long *) arg);

		case SOUND_MIXER_READ_CAPS:  /* 读取容量 */
			val = SOUND_CAP_EXCL_INPUT;
			return put_user(val, (long *) arg);

		case SOUND_MIXER_WRITE_VOLUME:  /* 控制声音 */
			ret = get_user(val, (long *) arg); /* 从用户空间拷贝数据 */
			if (ret)
				return ret;
			uda1341_volume = 63 - (((val & 0xff) + 1) * 63) / 100; /* 将数据转换为声音 控制数据*/
			uda1341_l3_address(UDA1341_REG_DATA0);
			uda1341_l3_data(uda1341_volume);
			break;

		case SOUND_MIXER_READ_VOLUME:  /* 读取音量 */
			val = ((63 - uda1341_volume) * 100) / 63;
			val |= val << 8;
			return put_user(val, (long *) arg);

		case SOUND_MIXER_READ_IGAIN:  /* 读取增益 */
			val = ((31- mixer_igain) * 100) / 31;
			return put_user(val, (int *) arg);

		case SOUND_MIXER_WRITE_IGAIN:  /* 控制增益 */
			ret = get_user(val, (int *) arg);   /* 从用户空间拷贝数据 */
			if (ret)
				return ret;
			mixer_igain = 31 - (val * 31 / 100);
			/* use mixer gain channel 1*/
			uda1341_l3_address(UDA1341_REG_DATA0);
			uda1341_l3_data(EXTADDR(EXT0));  /* EXTADDR(EXT0)=1100 0000 */
			uda1341_l3_data(EXTDATA(EXT0_CH1_GAIN(mixer_igain)));  /* mixer gain channel 1 */
			break;

		default:
			DPRINTK("mixer ioctl %u unknown\n", cmd);
			return -ENOSYS;
	}

	audio_mix_modcnt++;
	return 0;
}

/* 计算IISPSR寄存器的值*/
static int iispsr_value(int s_bit_clock, int sample_rate)  /*  iispsr_value(384 , 44100)   */
{
	int i, prescaler = 0;
	unsigned long tmpval;
	unsigned long tmpval384;
	unsigned long tmpval384min = 0xffff;

	tmpval384 = clk_get_rate(iis_clock) / s_bit_clock;    /* fs=获取的iis时钟/384 =50MHz/384=136533*/
//	tmpval384 = s3c2410_get_bus_clk(GET_PCLK) / s_bit_clock;

	for (i = 0; i < 32; i++) {
		tmpval = tmpval384/(i+1);
		if (PCM_ABS((sample_rate - tmpval)) < tmpval384min) {/*( 44100-(136533/i)<0 ? (136533/i)-44100 : 44100-(136533/i) )<65535*/
			tmpval384min = PCM_ABS((sample_rate - tmpval));
			prescaler = i;  /* i=3 */
		}
	}

	DPRINTK("prescaler = %d\n", prescaler);

	return prescaler; /* 3 */
}


/* 设置预分频 
     IISCLK的频率＝声道数×采样频率×采样位数，如采样频率fs为44.1kHz，采样的位数为16位，声道数2个（左、右两个声道），
     则IISSCLK的频率＝32fs＝1411.2kHz。

     IISLRCK为帧时钟，用于切换左、右声道，如IISLRCK为高电平表示正在传输的是左声道数据，为低电平表示正在传输的是右
     声道数据，因此IISLRCK的频率应该正好等于采样频率。*/
static long audio_set_dsp_speed(long val)
{
	unsigned int prescaler;
	prescaler=(IISPSR_A(iispsr_value(S_CLOCK_FREQ, val))
			| IISPSR_B(iispsr_value(S_CLOCK_FREQ, val)));  /* IISPSR_A(iispsr_value(S_CLOCK_FREQ, 44100)) = IISPSR_A(iispsr_value(384, 44100)) = (一个0～31 之间的值)<<5 预分频控制器A，用于内部时钟块 */
	__raw_writel(prescaler, iis_base + S3C2410_IISPSR);  /* IISPSR_B(iispsr_value(S_CLOCK_FREQ, 44100))) = (一个0～31 之间的值)<<0 预分频控制器B，用于外部时钟块 */

	printk(PFX "audio_set_dsp_speed:%ld prescaler:%i\n",val,prescaler);
	return (audio_rate = val);
}

/* dsp ioctl */
static int smdk2410_audio_ioctl(struct inode *inode, struct file *file,
				uint cmd, ulong arg)
{
	long val;

	switch (cmd) {
		case SNDCTL_DSP_SETFMT:  /* 设置采样格式 */
			get_user(val, (long *) arg);
			if (val & AUDIO_FMT_MASK) {
				audio_fmt = val;
				break;
			} else
				return -EINVAL;

		case SNDCTL_DSP_CHANNELS: /* 设置通道 */
		case SNDCTL_DSP_STEREO:  /* 设置立体声 */
			get_user(val, (long *) arg);
			if (cmd == SNDCTL_DSP_STEREO)
				val = val ? 2 : 1;
			if (val != 1 && val != 2)
				return -EINVAL;
			audio_channels = val;
			break;

		case SOUND_PCM_READ_CHANNELS:  /* 读通道信息 */
			put_user(audio_channels, (long *) arg);
			break;

		case SNDCTL_DSP_SPEED:  /* 设置速率 */
			get_user(val, (long *) arg);
			val = audio_set_dsp_speed(val);
			if (val < 0)
				return -EINVAL;
			put_user(val, (long *) arg);
			break;

		case SOUND_PCM_READ_RATE:  /* 读取速率 */
			put_user(audio_rate, (long *) arg);
			break;

		case SNDCTL_DSP_GETFMTS:  /* 获取格式掩码 */
			put_user(AUDIO_FMT_MASK, (long *) arg);
			break;

		case SNDCTL_DSP_GETBLKSIZE:  /* 获取缓冲区片段大小 */
			if(file->f_mode & FMODE_WRITE)
				return put_user(audio_fragsize, (long *) arg);
			else
				return put_user(audio_fragsize, (int *) arg);

		case SNDCTL_DSP_SETFRAGMENT:   /* 设置片段 */
			if (file->f_mode & FMODE_WRITE) { /* 写 */
				if (output_stream.buffers)
					return -EBUSY;
				get_user(val, (long *) arg);
				audio_fragsize = 1 << (val & 0xFFFF);
				if (audio_fragsize < 16)
					audio_fragsize = 16;
				if (audio_fragsize > 16384)
					audio_fragsize = 16384;
				audio_nbfrags = (val >> 16) & 0x7FFF;
				if (audio_nbfrags < 2)
					audio_nbfrags = 2;
				if (audio_nbfrags * audio_fragsize > 128 * 1024)
					audio_nbfrags = 128 * 1024 / audio_fragsize;
				if (audio_setup_buf(&output_stream))
					return -ENOMEM;

			}
			if (file->f_mode & FMODE_READ) { /* 读 */
				if (input_stream.buffers)
					return -EBUSY;
				get_user(val, (int *) arg);
				audio_fragsize = 1 << (val & 0xFFFF);
				if (audio_fragsize < 16)
					audio_fragsize = 16;
				if (audio_fragsize > 16384)
					audio_fragsize = 16384;
				audio_nbfrags = (val >> 16) & 0x7FFF;
				if (audio_nbfrags < 2)
					audio_nbfrags = 2;
				if (audio_nbfrags * audio_fragsize > 128 * 1024)
					audio_nbfrags = 128 * 1024 / audio_fragsize;
				if (audio_setup_buf(&input_stream))
					return -ENOMEM;

			}
			break;

		case SNDCTL_DSP_SYNC:  /* 设置同步 */
			return audio_sync(file);

		case SNDCTL_DSP_GETOSPACE:  /* 获取播放缓冲区空间信息 */
		{
			audio_stream_t *s = &output_stream;
			audio_buf_info *inf = (audio_buf_info *) arg;
			int err = access_ok(VERIFY_WRITE, inf, sizeof(*inf)); /* 检查用户空间的地址有效性 */
			int i;
			int frags = 0, bytes = 0;

			if (err)
				return err;
			for (i = 0; i < s->nbfrags; i++) {
				if (atomic_read(&s->buffers[i].sem.count) > 0) {  /* 持有信号量 */
					if (s->buffers[i].size == 0) frags++;
					bytes += s->fragsize - s->buffers[i].size;
				}
			}
			put_user(frags, &inf->fragments);
			put_user(s->nbfrags, &inf->fragstotal);
			put_user(s->fragsize, &inf->fragsize);
			put_user(bytes, &inf->bytes);
			break;
		}

		case SNDCTL_DSP_GETISPACE: /* 获取录音缓冲区空间信息 */
		{
			audio_stream_t *s = &input_stream;
			audio_buf_info *inf = (audio_buf_info *) arg;
			int err = access_ok(VERIFY_WRITE, inf, sizeof(*inf));
			int i;
			int frags = 0, bytes = 0;

			if (!(file->f_mode & FMODE_READ))
				return -EINVAL;

			if (err)
				return err;
			for(i = 0; i < s->nbfrags; i++){
				if (atomic_read(&s->buffers[i].sem.count) > 0)
				{
					if (s->buffers[i].size == s->fragsize)
						frags++;
					bytes += s->buffers[i].size;
				}
			}
			put_user(frags, &inf->fragments);
			put_user(s->nbfrags, &inf->fragstotal);
			put_user(s->fragsize, &inf->fragsize);
			put_user(bytes, &inf->bytes);
			break;
		}
		case SNDCTL_DSP_RESET: /* 复位*/
			if (file->f_mode & FMODE_READ) {
				audio_clear_buf(&input_stream);
			}
			if (file->f_mode & FMODE_WRITE) {
				audio_clear_buf(&output_stream);
			}
			return 0;
		case SNDCTL_DSP_NONBLOCK:  /* 设置无阻塞 */
			file->f_flags |= O_NONBLOCK;
			return 0;
		case SNDCTL_DSP_POST:
		case SNDCTL_DSP_SUBDIVIDE:
		case SNDCTL_DSP_GETCAPS:
		case SNDCTL_DSP_GETTRIGGER:
		case SNDCTL_DSP_SETTRIGGER:
		case SNDCTL_DSP_GETIPTR:
		case SNDCTL_DSP_GETOPTR:
		case SNDCTL_DSP_MAPINBUF:
		case SNDCTL_DSP_MAPOUTBUF:
		case SNDCTL_DSP_SETSYNCRO:
		case SNDCTL_DSP_SETDUPLEX:
			return -ENOSYS;
		default:
			return smdk2410_mixer_ioctl(inode, file, cmd, arg);
	}

	return 0;
}

/* 打开dsp */
static int smdk2410_audio_open(struct inode *inode, struct file *file)
{
	int cold = !audio_active;

	DPRINTK("audio_open\n");
	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (audio_rd_refcount || audio_wr_refcount)  /*说明已经打开 */
			return -EBUSY;
		audio_rd_refcount++;
	} else if ((file->f_flags & O_ACCMODE) == O_WRONLY) { /*说明已经打开 */
		if (audio_wr_refcount)
			return -EBUSY;
		audio_wr_refcount++;
	} else if ((file->f_flags & O_ACCMODE) == O_RDWR) {/*说明已经打开 */
		if (audio_rd_refcount || audio_wr_refcount)
			return -EBUSY;
		audio_rd_refcount++;
		audio_wr_refcount++;
	} else
		return -EINVAL;

		if (cold) {
			audio_rate = AUDIO_RATE_DEFAULT; /* 设置默认的采用频率为44.1KHz */
			audio_channels = AUDIO_CHANNELS_DEFAULT;  /* 设置默认的通道 */
			audio_fragsize = AUDIO_FRAGSIZE_DEFAULT;  /* 设置默认的片段大小 */
			audio_nbfrags = AUDIO_NBFRAGS_DEFAULT;   /* 设置默认的片段数 */
			if ((file->f_mode & FMODE_WRITE)){ /* 打开文件用于写 */
				init_s3c2410_iis_bus_tx();  /* 初始化iis总线----用于播放 */
				audio_clear_buf(&output_stream);  /* 清空输出缓冲区 */
			}
			if ((file->f_mode & FMODE_READ)){  /* 打开文件用于读 */
				init_s3c2410_iis_bus_rx(); /* 初始化iis总线----用于录音 */
				audio_clear_buf(&input_stream);  /* 清空输入缓冲区 */
			}
		}
		return 0;
}


static int smdk2410_mixer_open(struct inode *inode, struct file *file)
{
	return 0;
}

/*  */
static int smdk2410_audio_release(struct inode *inode, struct file *file)
{
	DPRINTK("audio_release\n");

	if (file->f_mode & FMODE_READ) { /* 读 */
		if (audio_rd_refcount == 1)
			audio_clear_buf(&input_stream);  /* 清空读缓冲区 */
		audio_rd_refcount = 0;
	}

	if(file->f_mode & FMODE_WRITE) { /* 写 */
		if (audio_wr_refcount == 1) {
			audio_sync(file);
			audio_clear_buf(&output_stream);  /* 清空写缓冲区 */
			audio_wr_refcount = 0;
		}
	}

	return 0;
}


static int smdk2410_mixer_release(struct inode *inode, struct file *file)
{
	return 0;
}


static struct file_operations smdk2410_audio_fops = {  /* 音频编解码设备操作函数 */
	.llseek=smdk2410_audio_llseek,
	.write= smdk2410_audio_write,
	.read= smdk2410_audio_read,
	.poll=smdk2410_audio_poll,
	.ioctl=smdk2410_audio_ioctl,
	.open=smdk2410_audio_open,
	.release=smdk2410_audio_release
};

static struct file_operations smdk2410_mixer_fops = { /* 混音器设备操作函数 */
	.ioctl=smdk2410_mixer_ioctl,
	.open=smdk2410_mixer_open,
	.release=smdk2410_mixer_release
};

/* 初始化uda1341 */
static void init_uda1341(void)
{

	/* GPB 4: L3CLOCK */
	/* GPB 3: L3DATA */
	/* GPB 2: L3MODE */

	unsigned long flags;

	uda1341_volume = 62 - ((DEF_VOLUME * 61) / 100);
	uda1341_boost = 0;
//        uda_sampling = DATA2_DEEMP_NONE;
//        uda_sampling &= ~(DATA2_MUTE);


	local_irq_save(flags);

	s3c2410_gpio_setpin(S3C2410_GPB2,1);/*L3MODE=1 */
	s3c2410_gpio_setpin(S3C2410_GPB4,1);/*L3CLOCK=1*/
	local_irq_restore(flags);

	uda1341_l3_address(UDA1341_REG_STATUS);  /* 数据传输模式选择STATUS，UDA1341_REG_STATUS=0x16 */
	uda1341_l3_data(0x40 | STAT0_SC_384FS | STAT0_IF_MSB|STAT0_DC_FILTER); /*写数据01011001---->复位,384fs,MSB-justified,DC滤波*/
	uda1341_l3_data(STAT1 | STAT1_ADC_ON | STAT1_DAC_ON);  /* 写数据10000011---->ADC on,DAC on */

	uda1341_l3_address(UDA1341_REG_DATA0);  /* 数据传输模式选择DATA0    */
	uda1341_l3_data(DATA0 |DATA0_VOLUME(0x0)); /* 写数据0x00--->设置音量为0dB(最大) */
	uda1341_l3_data(DATA1 |DATA1_BASS(uda1341_boost)| DATA1_TREBLE(0));  /* 写0x40---->BASS BOOST(低音增强)=0dB */
	uda1341_l3_data((DATA2 |DATA2_DEEMP_NONE) &~(DATA2_MUTE));  /* 写0x80---->Peak detection position选择before tone features, no de-emphasis, 无静音,模式开关
	                                                                                                                选择适中的*/
	uda1341_l3_data(EXTADDR(EXT2)); /* 写1100 0010---->扩展地址EA2~EA0为010 */
#if 0
	/* 对于MINI2440,  */
	uda1341_l3_data(EXTDATA(EXT2_MIC_GAIN(0x6)) | EXT2_MIXMODE_CH2);//input channel 2 select
#else
	/* 对于TQ2440 */
	uda1341_l3_data(EXTDATA(EXT2_MIC_GAIN(0x6)) | EXT2_MIXMODE_CH1);/*写1111 1001---->扩展数据，ED4~ED0=11001   ===>EA2~EA0-ED4~ED0=01011001---->
                                                                                                                                 MIC 放大器增益为+27;input channel 1 select (input channel 2 off)---因为MIC接到了通道1上*/
#endif
}

/* 初始化iis总线 */
static void init_s3c2410_iis_bus(void){
	__raw_writel(0, iis_base + S3C2410_IISPSR);   /* 清零预分频寄存器IISPSR */
	__raw_writel(0, iis_base + S3C2410_IISCON);  /* 清零控制寄存器IISCON */
	__raw_writel(0, iis_base + S3C2410_IISMOD);  /* 清零模式寄存器IISMOD */
	__raw_writel(0, iis_base + S3C2410_IISFCON); /* 清零FIFO控制寄存器IISFCON */
	clk_disable(iis_clock);  /* 禁止时钟 */
}

 /* 初始化iis总线----用于录音 */
static void init_s3c2410_iis_bus_rx(void){
	unsigned int iiscon, iismod, iisfcon;
	char *dstr;
//Kill everything...
	__raw_writel(0, iis_base + S3C2410_IISPSR);
	__raw_writel(0, iis_base + S3C2410_IISCON);
	__raw_writel(0, iis_base + S3C2410_IISMOD);
	__raw_writel(0, iis_base + S3C2410_IISFCON);

	clk_enable(iis_clock);

	iiscon = iismod = iisfcon = 0;

//Setup basic stuff
	iiscon |= S3C2410_IISCON_PSCEN; /* 使能iis预分频器*/
	iiscon |= S3C2410_IISCON_IISEN;  /* 使能iis接口*/

//	iismod |= S3C2410_IISMOD_MASTER; // Set interface to Master Mode
	iismod |= S3C2410_IISMOD_LR_LLOW; /* 主机模式----IISLRCK和IISCLK为输出模式*/
	iismod |= S3C2410_IISMOD_MSB;        /* 设置MSB对齐格式*/
	iismod |= S3C2410_IISMOD_16BIT;      /* 设置串行数据格式为16位*/
	iismod |= S3C2410_IISMOD_384FS;      /* 设置主时钟频率为384fs*/
	iismod |= S3C2410_IISMOD_32FS;       /* 设置串行位时钟频率为32fs*/


	iisfcon |= S3C2410_IISFCON_RXDMA; /* 设置接收FIFO访问模式为DMA*/
	iisfcon |= S3C2410_IISFCON_TXDMA; /* 设置发送FIFO访问模式为DMA*/

	iiscon |= S3C2410_IISCON_RXDMAEN; /* 使能接收DMA服务请求*/
	iiscon |= S3C2410_IISCON_TXIDLE;     /* 暂停Tx*/
	iismod |= S3C2410_IISMOD_RXMODE; /*接收模式*/
	iisfcon |= S3C2410_IISFCON_RXENABLE; /* 使能接收FIFO*/
	dstr="RX";
//setup the prescaler
	audio_set_dsp_speed(audio_rate);  /* 设置预分频 */

//iiscon has to be set last - it enables the interface
	__raw_writel(iismod, iis_base + S3C2410_IISMOD);
	__raw_writel(iisfcon, iis_base + S3C2410_IISFCON);
	__raw_writel(iiscon, iis_base + S3C2410_IISCON);
}

/* 初始化iis总线----用于播放 */
static void init_s3c2410_iis_bus_tx(void)
{
	unsigned int iiscon, iismod, iisfcon;
	char *dstr;

//Kill everything...
       /* 清空 */
	__raw_writel(0, iis_base + S3C2410_IISPSR);  
	__raw_writel(0, iis_base + S3C2410_IISCON);
	__raw_writel(0, iis_base + S3C2410_IISMOD);
	__raw_writel(0, iis_base + S3C2410_IISFCON);

	clk_enable(iis_clock);  /* 使能时钟 */


	iiscon = iismod = iisfcon = 0;

//Setup basic stuff
	iiscon |= S3C2410_IISCON_PSCEN; /* 使能iis预分频器*/
	iiscon |= S3C2410_IISCON_IISEN;  /* 使能iis接口*/

//	iismod |= S3C2410_IISMOD_MASTER; // Set interface to Master Mode
	iismod |= S3C2410_IISMOD_LR_LLOW; /* 主机模式----IISLRCK和IISCLK为输出模式*/
	iismod |= S3C2410_IISMOD_MSB;        /* 设置MSB对齐格式*/
	iismod |= S3C2410_IISMOD_16BIT;      /* 设置串行数据格式为16位*/
	iismod |= S3C2410_IISMOD_384FS;      /* 设置主时钟频率为384fs*/
	iismod |= S3C2410_IISMOD_32FS;        /* 设置串行位时钟频率为32fs*/

	iisfcon|= S3C2410_IISFCON_RXDMA;  /* 设置接收FIFO访问模式为DMA*/
	iisfcon|= S3C2410_IISFCON_TXDMA;  /* 设置发送FIFO访问模式为DMA*/

	iiscon |= S3C2410_IISCON_TXDMAEN; /* 使能发送DMA服务请求*/
	iiscon |= S3C2410_IISCON_RXIDLE;    /* 暂停Rx*/
	iismod |= S3C2410_IISMOD_TXMODE; /*发送模式*/
	iisfcon|= S3C2410_IISFCON_TXENABLE; /* 使能发送FIFO*/
	dstr="TX";

//setup the prescaler
	audio_set_dsp_speed(audio_rate);


//iiscon has to be set last - it enables the interface
	__raw_writel(iismod, iis_base + S3C2410_IISMOD);
	__raw_writel(iisfcon, iis_base + S3C2410_IISFCON);
	__raw_writel(iiscon, iis_base + S3C2410_IISCON);
}

/* 初始化音频使用的DMA */
static int __init audio_init_dma(audio_stream_t * s, char *desc)
{
	int ret ;
	enum s3c2410_dmasrc source;
	int hwcfg;
	unsigned long devaddr;
	unsigned char channel;
	int dcon;
	unsigned int flags = 0;

	if(s->dma_ch == DMACH_I2S_OUT){  /* 输出通道 */
		channel = DMACH_I2S_OUT;
		source  = S3C2410_DMASRC_MEM; /* 源在系统总线上 */
		hwcfg   = (S3C2410_DISRCC_APB|S3C2410_DISRCC_INC);  /* 目的在外设总线上;地址固定 */
		devaddr = 0x55000010;   /* 设备地址----IIS FIFO寄存器的地址 */
		dcon    = S3C2410_DCON_HANDSHAKE|S3C2410_DCON_SYNC_PCLK|S3C2410_DCON_INTREQ|(0<<28)|(0<<27)|S3C2410_DCON_CH2_I2SSDO|S3C2410_DCON_HWTRIG; // VAL: 0xa0800000;
		flags   = S3C2410_DMAF_AUTOSTART;

		ret = s3c2410_dma_request(channel , &s3c2410iis_dma_out, NULL); /* 申请 某通道的DMA资源*/
		s3c2410_dma_devconfig(channel, source, hwcfg, devaddr); /* 配置设备的DMA */
		s3c2410_dma_config(channel, 2, dcon);  /* 配置DMA，参数2表示2个字节(查看IISFIFO寄存器) */
		s3c2410_dma_set_buffdone_fn(channel, audio_dmaout_done_callback);  /* 设置缓冲区完成回调函数 */
		s3c2410_dma_setflags(channel, flags); /* 设置DMA标记----如果使用缓冲区队列则自动启动 */
              s->dma_ok = 1;
		return ret;
	}
	else if(s->dma_ch == DMACH_I2S_IN){ /* 输入 */
		channel = DMACH_I2S_IN;      
		source  = S3C2410_DMASRC_HW;  /* 源在硬件设备 */
		hwcfg   = ~(S3C2410_DISRCC_APB|S3C2410_DISRCC_INC);  /* 目的在系统总线上，地址增加 */
		devaddr = 0x55000010;
		dcon    = S3C2410_DCON_HANDSHAKE|S3C2410_DCON_SYNC_PCLK|S3C2410_DCON_INTREQ|(0<<28)|(0<<27)|S3C2410_DCON_CH1_I2SSDI|S3C2410_DCON_HWTRIG; // VAL: 0xa2800000;
		flags   = S3C2410_DMAF_AUTOSTART;

		ret = s3c2410_dma_request(s->dma_ch, &s3c2410iis_dma_in, NULL);
		s3c2410_dma_devconfig(channel, source, hwcfg, devaddr);
		s3c2410_dma_config(channel, 2, dcon);
		s3c2410_dma_set_buffdone_fn(channel, audio_dmain_done_callback);
		s3c2410_dma_setflags(channel, flags);
		s->dma_ok =1;
		return ret ;
	}
	else
		return 1;
}

static int audio_clear_dma(audio_stream_t * s, struct s3c2410_dma_client *client)
{
	s3c2410_dma_set_buffdone_fn(s->dma_ch, NULL); /* 清除回调函数 */
	s3c2410_dma_free(s->dma_ch, client);  /* 释放DMA */
	return 0;
}

/*
  devs.c:
  static struct resource s3c_iis_resource[] = {  // IIS设备资源 
	[0] = {
		.start = S3C24XX_PA_IIS,       //0x55000000 
		.end   = S3C24XX_PA_IIS + S3C24XX_SZ_IIS -1,    //0x55100000-1
		.flags = IORESOURCE_MEM,
	}
};

static u64 s3c_device_iis_dmamask = 0xffffffffUL;
 
struct platform_device s3c_device_iis = {  //IIS设备
	.name		  = "s3c2440-iis",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c_iis_resource),
	.resource	  = s3c_iis_resource,
	.dev              = {
		.dma_mask = &s3c_device_iis_dmamask,
		.coherent_dma_mask = 0xffffffffUL
	}
};
  */

static int s3c2410iis_probe(struct device *dev) 
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	unsigned long flags;

	printk ("s3c2410iis_probe...\n");
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);  /*获取资源 */
	if (res == NULL) {
		printk(KERN_INFO PFX "failed to get memory region resouce\n");
		return -ENOENT;
	}

	iis_base = (void *)S3C24XX_PA_IIS ;  /* 0x55000000 */
	if (iis_base == 0) {
		printk(KERN_INFO PFX "failed to ioremap() region\n");
		return -EINVAL;
	}

	iis_clock = clk_get(dev, "iis");  /* 获取iis控制器时钟 */
	if (iis_clock == NULL) {
		printk(KERN_INFO PFX "failed to find clock source\n");
		return -ENOENT;
	}

	clk_enable(iis_clock);  /* 使能iis时钟 */

	local_irq_save(flags);
	
	/* GPB 4: L3CLOCK, OUTPUT */
	/* 配置引脚 */
	s3c2410_gpio_cfgpin(S3C2410_GPB4, S3C2410_GPB4_OUTP);
	s3c2410_gpio_pullup(S3C2410_GPB4,1);
	/* GPB 3: L3DATA, OUTPUT */
	s3c2410_gpio_cfgpin(S3C2410_GPB3,S3C2410_GPB3_OUTP);
	/* GPB 2: L3MODE, OUTPUT */
	s3c2410_gpio_cfgpin(S3C2410_GPB2,S3C2410_GPB2_OUTP);
	s3c2410_gpio_pullup(S3C2410_GPB2,1);
	/* GPE 3: I2SSDI */
	s3c2410_gpio_cfgpin(S3C2410_GPE3,S3C2410_GPE3_I2SSDI);
	s3c2410_gpio_pullup(S3C2410_GPE3,0);
	/* GPE 0: I2SLRCK */
	s3c2410_gpio_cfgpin(S3C2410_GPE0,S3C2410_GPE0_I2SLRCK);
	s3c2410_gpio_pullup(S3C2410_GPE0,0);
	/* GPE 1: I2SSCLK */
	s3c2410_gpio_cfgpin(S3C2410_GPE1,S3C2410_GPE1_I2SSCLK);
	s3c2410_gpio_pullup(S3C2410_GPE1,0);
	/* GPE 2: CDCLK */
	s3c2410_gpio_cfgpin(S3C2410_GPE2,S3C2410_GPE2_CDCLK);
	s3c2410_gpio_pullup(S3C2410_GPE2,0);
	/* GPE 4: I2SSDO */
	s3c2410_gpio_cfgpin(S3C2410_GPE4,S3C2410_GPE4_I2SSDO);
	s3c2410_gpio_pullup(S3C2410_GPE4,0);

	local_irq_restore(flags);

	init_s3c2410_iis_bus();  /* 初始化iis总线 */

	init_uda1341();  /* 初始化uda1341 */

	output_stream.dma_ch = DMACH_I2S_OUT;  /* 设置DMA通道 */
	if (audio_init_dma(&output_stream, "UDA1341 out")) {
		audio_clear_dma(&output_stream,&s3c2410iis_dma_out);
		printk( KERN_WARNING AUDIO_NAME_VERBOSE
				": unable to get DMA channels\n" );
		return -EBUSY;
	}
    
	input_stream.dma_ch = DMACH_I2S_IN;
	if (audio_init_dma(&input_stream, "UDA1341 in")) {  /* 初始化DMA */
		audio_clear_dma(&input_stream,&s3c2410iis_dma_in);
		printk( KERN_WARNING AUDIO_NAME_VERBOSE
				": unable to get DMA channels\n" );
		return -EBUSY;
	}

	audio_dev_dsp = register_sound_dsp(&smdk2410_audio_fops, -1); /* 注册DSP----音频编解码设备 */
	audio_dev_mixer = register_sound_mixer(&smdk2410_mixer_fops, -1);  /* 注册mixer----混音器---控制 */

	printk(AUDIO_NAME_VERBOSE " initialized\n"); 

	return 0;
}


static int s3c2410iis_remove(struct device *dev) {
	unregister_sound_dsp(audio_dev_dsp);  /* 注销DSP */
	unregister_sound_mixer(audio_dev_mixer); /* 注销mixer */
	audio_clear_dma(&output_stream,&s3c2410iis_dma_out);  /* 清除用于音频输出的dma */
	audio_clear_dma(&input_stream,&s3c2410iis_dma_in); /* input */  /* 清除用于音频输入的dma */
	printk(AUDIO_NAME_VERBOSE " unloaded\n");
	return 0;
}

extern struct bus_type platform_bus_type;
static struct device_driver s3c2410iis_driver = {
	.name = "s3c2410-iis",
	.bus = &platform_bus_type,
	.probe = s3c2410iis_probe,
	.remove = s3c2410iis_remove,
};

static int __init s3c2410_uda1341_init(void) {
	memzero(&input_stream, sizeof(audio_stream_t));
	memzero(&output_stream, sizeof(audio_stream_t));
	return driver_register(&s3c2410iis_driver);
}

static void __exit s3c2410_uda1341_exit(void) {
	driver_unregister(&s3c2410iis_driver);
}


module_init(s3c2410_uda1341_init);
module_exit(s3c2410_uda1341_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("superlp<xinshengtaier@eyou.com>");
MODULE_DESCRIPTION("S3C2410 uda1341 sound driver");

