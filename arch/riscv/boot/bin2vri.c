/*
 * bin2vri
 *
 * Based on the ELF2VRI converter 2025-06-19 VLSI Solution.
 *
 * This source code may be freely used and modified as long as it has
 * something to do with VLSI Solution's RISC-V chips.
 *
 * FULL DOCUMENTATION FOR THE VRI FORMAT:
 *
 * "VRI1"
 * Section0
 * ...
 * SectionN-1
 *
 * Sections can be either literal run or RLE zero run:
 *
 * Section, literal run:
 *     56 78 12 34	ADDR: Address (mixed-endian, value is 0x12345678)
 *     56 78 12 34	SIZE: Literal run size in bytes, must be divisable by 4
 *     00 0? 00 00	FLAGS: FL_READ 1, FL_WRITE 2, FL_EXECUTE 4, no FL_BSS 8
 *     00 00 00 00	EXTENSION: 0
 *   n*56 78 12 34	Literal data, n=SIZE/4, file bytes: 0x12 0x34 0x56 0x78
 *
 * Section, RLE zero run:
 *     56 78 12 34	ADDR: Address (mixed-endian, value is 0x12345678)
 *     56 78 12 34	SIZE: RLE zero run size in bytes, must be divisable by 4
 *     00 0? 00 00	FLAGS: FL_READ 1, FL_WRITE 2, FL_EXECUTE 4, set FL_BSS 8
 *     00 00 00 00	EXTENSION: 0
 *
 * Note!
 * Sections are independent of each other so new sections can be
 * appended to an existing image with no issues.
 *
 * Note!
 * All 32-bit values are encoded in a similar mixed-endian way.
 *
 * Note!
 * Literal and RLE zero runs are recognized by the FL_BSS bit: if FL_BSS is set
 * the section is of type RLE zero run. If FL_BSS is clear then section is a
 * literal run.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>

#define RLE_RUN_LIMIT_32BIT_WORDS 0x80

#define FL_READ    1
#define FL_WRITE   2
#define FL_EXECUTE 4
#define FL_BSS     8

struct state {
	FILE *ofp;
	const char *me;
	const char *input;
	unsigned int addr;
	unsigned int flags;
	unsigned int insize;
	unsigned int outsize;
	bool verbose;
	bool raw;
};

static void Put32HeadbangEndian(struct state *s, unsigned int i)
{
	fprintf(s->ofp, "%c%c%c%c", (i >> 16) & 0xffU, (i >> 24) & 0xffU,
		(i >> 0) & 0xffU, (i >> 8) & 0xffU);
	s->outsize += 4;
}

static void Put32MixedEndian(struct state *s, unsigned int i)
{
	fprintf(s->ofp, "%c%c%c%c", (i >> 8) & 0xffU, (i >> 0) & 0xffU,
		(i >> 24) & 0xffU, (i >> 16) & 0xffU);
	s->outsize += 4;
}

static unsigned int PurgeSections(struct state *s, unsigned int *data, unsigned int addr,
                                  int literal, int rle)
{
	if (s->verbose) {
		printf("    RLE: a=%08x, lit %6x, bss %6x\n", addr,
		       4 * literal, 4 * rle);
	}

	if (literal) {
		Put32MixedEndian(s, addr);
		Put32MixedEndian(s, 4 * literal);
		Put32MixedEndian(s, s->flags);
		Put32MixedEndian(s, 0); /* future expansion */

		for (int i = 0; i < literal; i++)
			Put32HeadbangEndian(s, *data++);

		addr += 4 * literal;
	}

	if (rle) {
		Put32MixedEndian(s, addr);
		Put32MixedEndian(s, 4 * rle);
		Put32MixedEndian(s, s->flags | FL_BSS);
		Put32MixedEndian(s, 0); /* future expansion */

		data += rle;
		addr += 4 * rle;
	}
	return addr;
}

static void DumpSection(struct state *s, uint32_t *words, int nwords)
{
	unsigned int addr = s->addr;
	int base = 0;
	int rle = 0;
	int i;

	for (i = 0; i < nwords; i++) {
		if (words[i]) {
			if (rle >= RLE_RUN_LIMIT_32BIT_WORDS) {
				int literal = i - base - rle;

				/* align literalRun to 2 longs = 8 bytes */
				if ((literal & 1) && rle) {
					rle--;
					literal++;
				}
				/* align RLE to 2 longs = 8 bytes */
				if (rle & 1) {
					i--;
					rle--;
				}

				addr = PurgeSections(s, words + base, addr,
				                     literal, rle);
				base = i;
			}
			rle = 0;
		} else {
			rle++;
		}
	}
	{
		int literal = i - base - rle;

		/* align literalRun to 2 longs = 8 bytes */
		if ((literal & 1) && rle) {
			rle--;
			literal++;
		}
		if (rle < RLE_RUN_LIMIT_32BIT_WORDS) {
			literal += rle;
			rle = 0;
		}
		addr = PurgeSections(s, words + base, addr, literal, rle);
		base = i;
	}
}

static int readwords(struct state *s, uint32_t **res)
{
	FILE *fp = fopen(s->input, "rb");
	uint32_t *words = NULL;
	unsigned int shift;
	uint32_t word;
	long filesize;
	int nwords;
	int ret;

	if (!fp) {
		fprintf(stderr, "%s: could not open %s\n", s->me, s->input);
		return -1;
	}

	ret = fseek(fp, 0, SEEK_END);
	if (ret)
		goto err;

	filesize = ftell(fp);
	if (filesize < 0)
		goto err;
	if (filesize > UINT32_MAX) {
		fprintf(stderr, "%s: input too big\n", s->me);
		return -1;
	}
	s->insize = filesize;
	nwords = (filesize + sizeof(*words) - 1) / sizeof(*words);

	ret = fseek(fp, 0, SEEK_SET);
	if (ret < 0)
		goto err;

	words = calloc(nwords, sizeof(*words));
	if (!words) {
		fprintf(stderr, "%s: out of memory\n", s->me);
		return -1;
	}

	shift = 32;
	word = 0;
	nwords = 0;
	for (; filesize; filesize--) {
		ret = fgetc(fp);
		if (ret < 0)
			goto err;

		shift -= 8;
		word |= ((uint32_t)ret) << shift;
		if (shift)
			continue;

		words[nwords++] = word;
		shift = 32;
		word = 0;
	}
	if (shift < 32)
		words[nwords++] = word;

	ret = fclose(fp);
	if (ret)
		goto err;

	*res = words;
	return nwords;
err:
	free(words);
	fprintf(stderr, "%s: error reading from %s\n", s->me, s->input);
	return -1;
}

static int HandleRawFile(struct state *s)
{
	uint32_t *words;
	int nwords;

	nwords = readwords(s, &words);
	if (nwords < 0)
		return nwords;

	DumpSection(s, words, nwords);

	free(words);

	return 0;
}

static int usage(FILE *out, const char *me)
{
	return fprintf(
		out,
		"\nusage: %s [-a addr] [-r] [-v] input [output]\n\n"
		"input    input file\n"
		"output   output file, omit or use \"-\" for stdout\n"
		"-a addr  set load address\n"
		"-r       enable raw output which can be appended to an existing VRI file\n"
		"-v       be verbose\n"
		"-h       show this help\n",
		me);
}

static int parse_args(int argc, char *argv[], struct state *s)
{
	const char *output;
	int ret;

	s->me = argv[0];
	s->flags = FL_READ | FL_WRITE | FL_EXECUTE;

	while ((ret = getopt(argc, argv, "a:rvh")) >= 0) {
		switch (ret) {
		case 'a':
			s->addr = strtol(optarg, NULL, 0);
			break;
		case 'r':
			s->raw = true;
			break;
		case 'v':
			s->verbose = true;
			break;
		case 'h':
			usage(stdout, s->me);
			return 1;
		default:
			usage(stderr, s->me);
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "%s: no input file given", s->me);
		return -1;
	}
	s->input = argv[optind++];

	if (optind >= argc) {
		s->ofp = stdout;
		s->verbose = false;
		return 0;
	}
	output = argv[optind++];
	if (!strcmp(output, "-")) {
		s->ofp = stdout;
		s->verbose = false;
	} else {
		s->ofp = fopen(output, "wb");
	}
	if (!s->ofp) {
		fprintf(stderr, "%s: could not open %s\n", s->me, output);
		return -1;
	}

	if (optind < argc) {
		fprintf(stderr, "%s: extraneous parameter \"%s\"\n", s->me,
			argv[optind]);
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct state s = {};
	int ret;

	ret = parse_args(argc, argv, &s);
	if (ret)
		goto out;

	if (!s.raw)
		fprintf(s.ofp, "VRI1");
	s.outsize += 4;

	ret = HandleRawFile(&s);
	if (ret)
		goto out;

	if (s.verbose) {
		printf("    Compressed %u bytes to %u (%.1f%% compression)\n",
		       s.insize, s.outsize,
		       s.insize ? 100.0 * (s.insize - s.outsize) / s.insize : 0.0);
	}

out:
	if (s.ofp && s.ofp != stdout)
		fclose(s.ofp);
	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
