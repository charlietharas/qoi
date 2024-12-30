#include "stdint.h"
#include "stdlib.h"
#include "stdio.h"

#define OP_INDEX 0x00
#define OP_RUN 0xc0
#define OP_DIFF 0x40
#define OP_LUMA 0x80
#define OP_RGB 0xfe
#define OP_RGBA 0xff

#define OP_INDEX_B 0b00
#define OP_DIFF_B 0b01
#define OP_LUMA_B 0b10
#define OP_RUN_B 0b11

struct pixel {
    uint8_t r, g, b, a;
};

struct qoi_header {
    char magic[4];
    uint32_t width;
    uint32_t height;
    uint8_t channels;
    uint8_t colorspace;
};

struct image_data {
    uint64_t numPixels;
    struct pixel* pixels;
};

// buffers are for losers
void encode(FILE* fptr, struct qoi_header header, struct image_data* data) {
    fwrite(&header.magic, sizeof(char), 4, fptr);
    fwrite(&header.width, sizeof(uint32_t), 1, fptr);
    fwrite(&header.height, sizeof(uint32_t), 1, fptr);
    fwrite(&header.channels, sizeof(uint8_t), 1, fptr);
    fwrite(&header.colorspace, sizeof(uint8_t), 1, fptr);

    struct pixel prev = { 0, 0, 0, 255 };
    struct pixel curr = prev;
    struct pixel index[64];
    int run = 0;

    for (int i = 0; i < data->numPixels; i++) {
        curr.r = data->pixels[i].r;
        curr.g = data->pixels[i].g;
        curr.b = data->pixels[i].b;
        if (header.channels == 4) {
            curr.a = data->pixels[i].a;
        }

        if (curr.r == prev.r && curr.g == prev.g && curr.b == prev.b && curr.a == prev.a) {
            run++;
            if (run == 62 || i == data->numPixels-1) {
                uint8_t b = OP_RUN | (run - 1);
                fwrite(&b, sizeof(uint8_t), 1, fptr);
                run = 0;
            }
        }
        else {
            if (run > 0) {
                uint8_t b = OP_RUN | (run - 1);
                fwrite(&b, sizeof(uint8_t), 1, fptr);
                run = 0;
            }

            int hashIndex = (curr.r * 3 + curr.g * 5 + curr.b * 7 + curr.a * 11) % 64;
            struct pixel hashPixel = index[hashIndex];
            if (hashPixel.r == curr.r && hashPixel.g == curr.g && hashPixel.b == curr.b && hashPixel.a == curr.a) {
                uint8_t b = OP_INDEX | hashIndex;
                fwrite(&b, sizeof(uint8_t), 1, fptr);
            }
            else {
                index[hashIndex].r = curr.r;
                index[hashIndex].g = curr.g;
                index[hashIndex].b = curr.b;
                index[hashIndex].a = curr.a;

                if (curr.a == prev.a) {
                    signed char dr = curr.r - prev.r;
                    signed char dg = curr.g - prev.g;
                    signed char db = curr.b - prev.b;
                    signed char dg_dr = dr - dg;
                    signed char dg_db = db - dg;

                    if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                        uint8_t b = OP_DIFF | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2);
                        fwrite(&b, sizeof(uint8_t), 1, fptr);
                    }
                    else if (dg_dr >= -8 && dg_dr < 7 && dg >= -32 && dg <= 31 && dg_db >= -8 && dg_db <= 7) {
                        uint8_t b1 = OP_LUMA | (dg + 32);
                        uint8_t b2 = ((dg_dr + 8) << 4) | (dg_db + 8);
                        fwrite(&b1, sizeof(uint8_t), 1, fptr);
                        fwrite(&b2, sizeof(uint8_t), 1, fptr);
                    }
                    else {
                        uint8_t b[4] = { OP_RGB, curr.r, curr.g, curr.b };
                        fwrite(&b, sizeof(uint8_t), 4, fptr);
                    }
                }
                else {
                    uint8_t b[5] = { OP_RGBA, curr.r, curr.g, curr.b, curr.a };
                    fwrite(&b, sizeof(uint8_t), 5, fptr);
                }
            }
        }

        prev = curr;
    }

    uint8_t marker[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    fwrite(&marker, sizeof(uint8_t), 8, fptr);
}

void decode(FILE* fptr, struct qoi_header* header, struct image_data* data) {
    fread(&header->magic, sizeof(char), 4, fptr);
    uint32_t width, height;
    fread(&width, sizeof(uint32_t), 1, fptr);
    fread(&height, sizeof(uint32_t), 1, fptr);
    int num = 1;
    char* ptr = (char*)&num;
    if (*ptr == 1) {
        // convert from little endian
        width = ((0x000000ff & width) << 24) | ((0x0000ff00 & width) << 8) | ((0x00ff0000 & width) >> 8) | ((0xff000000 & width) >> 24);
        height = ((0x000000ff & height) << 24) | ((0x0000ff00 & height) << 8) | ((0x00ff0000 & height) >> 8) | ((0xff000000 & height) >> 24);
    }
    int total = width * height;
    header->width = width;
    header->height = height;
    
    fread(&header->channels, sizeof(uint8_t), 1, fptr);
    fread(&header->colorspace, sizeof(uint8_t), 1, fptr);

    struct pixel* pixels = (struct pixel*)malloc(total * sizeof(struct pixel));
    
    struct pixel prev = { 0, 0, 0, 255 };
    struct pixel index[64];
    int run = 0;
    for (int i = 0; i < total; i++) {
        uint8_t nextbyte;
        fread(&nextbyte, sizeof(uint8_t), 1, fptr);
        if (nextbyte == OP_RGBA) {
            fread(&pixels[i].r, sizeof(uint8_t), 1, fptr);
            fread(&pixels[i].g, sizeof(uint8_t), 1, fptr);
            fread(&pixels[i].b, sizeof(uint8_t), 1, fptr);
            fread(&pixels[i].a, sizeof(uint8_t), 1, fptr);
        }
        else if (nextbyte == OP_RGB) {
            fread(&pixels[i].r, sizeof(uint8_t), 1, fptr);
            fread(&pixels[i].g, sizeof(uint8_t), 1, fptr);
            fread(&pixels[i].b, sizeof(uint8_t), 1, fptr);
            pixels[i].a = 255;
        }
        else {
            uint8_t header = nextbyte >> 6;
            struct pixel hashPixel;
            int hashIndex = nextbyte & 0x00ffffff;
            uint8_t secondbyte;
            switch (header) {
            case OP_INDEX_B:
                pixels[i] = index[hashIndex];
                break;
            case OP_DIFF_B:
                pixels[i].r = prev.r + ((nextbyte & 0x00ff0000 >> 4) - 2);
                pixels[i].g = prev.g + ((nextbyte & 0x0000ff00 >> 2) - 2);
                pixels[i].b = prev.b + ((nextbyte & 0x000000ff) - 2);
                pixels[i].a = prev.a;
                break;
            case OP_LUMA_B:
                pixels[i].g = prev.g + (hashIndex - 32);
                fread(&secondbyte, sizeof(uint8_t), 1, fptr);
                pixels[i].r = prev.r + ((secondbyte >> 4) - 8) + (hashIndex - 32);
                pixels[i].b = prev.b + ((secondbyte & 0x0000ffff) - 8) + (hashIndex - 32);
                pixels[i].a = prev.a;
                break;
            case OP_RUN_B:
                for (int j = 0; j < hashIndex + 1; j++) {
                    pixels[i + j] = prev;
                }
                i += hashIndex + 1;
                break;
            }

            if (header != OP_INDEX_B) {
                index[hashIndex] = pixels[i];
            }
        }
        prev = pixels[i];
    }
}

int main() {
    int w = 800;
    int h = 600;
    int t = w * h;

    struct pixel* pixels = (struct pixel*) malloc(t * sizeof(struct pixel));
    if (pixels == NULL) {
        return 1;
    }

    for (int i = 0; i < t; i++) {
        pixels[i].r = 255;
        pixels[i].g = 0;
        pixels[i].b = 0;
        pixels[i].a = 255;
    }
    struct image_data data = { t, pixels };

    int num = 1;
    char* ptr = (char*)&num;
    if (*ptr == 1) {
        // convert from little endian
        w = ((0x000000ff & w) << 24) | ((0x0000ff00 & w) << 8) | ((0x00ff0000 & w) >> 8) | ((0xff000000 & w) >> 24);
        h = ((0x000000ff & h) << 24) | ((0x0000ff00 & h) << 8) | ((0x00ff0000 & h) >> 8) | ((0xff000000 & h) >> 24);
    }
    struct qoi_header header = { {'q', 'o', 'i', 'f'}, w, h, 4, 1};
    
    FILE* writeptr = fopen("test.qoi", "wb");
    encode(writeptr, header, &data);

    FILE* readptr = fopen("test.qoi", "rb");
    decode(readptr, &header, &data);

    free(pixels);
}
