#include "../include/inflate.h"
#include "../string.h"

typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t position;
    uint32_t bits;
    uint8_t bit_count;
} bit_reader_t;

typedef struct {
    uint16_t count[16];
    uint16_t symbol[288];
} huffman_t;

typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint32_t length;
} inflate_output_t;

static bool read_bits(bit_reader_t *reader, uint8_t count, uint32_t *value) {
    uint32_t mask;
    if (!reader || !value || count > 24U) return false;
    while (reader->bit_count < count) {
        if (reader->position >= reader->length) return false;
        reader->bits |= (uint32_t)reader->data[reader->position++] <<
                        reader->bit_count;
        reader->bit_count = (uint8_t)(reader->bit_count + 8U);
    }
    mask = count == 0U ? 0U : ((1U << count) - 1U);
    *value = reader->bits & mask;
    reader->bits >>= count;
    reader->bit_count = (uint8_t)(reader->bit_count - count);
    return true;
}

static void align_byte(bit_reader_t *reader) {
    if (!reader) return;
    reader->bits = 0U;
    reader->bit_count = 0U;
}

static bool build_huffman(huffman_t *table, const uint8_t *lengths,
                          uint32_t count) {
    uint16_t offsets[16];
    uint32_t symbol;
    int left = 1;
    if (!table || !lengths || count > 288U) return false;
    memset(table, 0, sizeof(*table));
    for (symbol = 0U; symbol < count; symbol++) {
        if (lengths[symbol] > 15U) return false;
        table->count[lengths[symbol]]++;
    }
    if (table->count[0] == count) return true;
    for (symbol = 1U; symbol <= 15U; symbol++) {
        left <<= 1;
        left -= table->count[symbol];
        if (left < 0) return false;
    }
    offsets[1] = 0U;
    for (symbol = 1U; symbol < 15U; symbol++)
        offsets[symbol + 1U] = (uint16_t)(offsets[symbol] +
                                         table->count[symbol]);
    for (symbol = 0U; symbol < count; symbol++) {
        uint8_t length = lengths[symbol];
        if (length) table->symbol[offsets[length]++] = (uint16_t)symbol;
    }
    return true;
}

static int decode_symbol(bit_reader_t *reader, const huffman_t *table) {
    uint32_t code = 0U;
    uint32_t first = 0U;
    uint32_t index = 0U;
    uint32_t length;
    for (length = 1U; length <= 15U; length++) {
        uint32_t bit;
        uint32_t count;
        if (!read_bits(reader, 1U, &bit)) return -1;
        code |= bit;
        count = table->count[length];
        if (code >= first && code - first < count)
            return table->symbol[index + code - first];
        index += count;
        first = (first + count) << 1U;
        code <<= 1U;
    }
    return -1;
}

static bool output_byte(inflate_output_t *output, uint8_t value) {
    if (!output || output->length >= output->capacity) return false;
    output->data[output->length++] = value;
    return true;
}

static bool copy_distance(inflate_output_t *output, uint32_t distance,
                          uint32_t length) {
    uint32_t i;
    if (!output || distance == 0U || distance > output->length ||
        length > output->capacity - output->length) return false;
    for (i = 0U; i < length; i++) {
        uint8_t value = output->data[output->length - distance];
        output->data[output->length++] = value;
    }
    return true;
}

static bool fixed_tables(huffman_t *literal, huffman_t *distance) {
    uint8_t literal_lengths[288];
    uint8_t distance_lengths[32];
    uint32_t i;
    for (i = 0U; i <= 143U; i++) literal_lengths[i] = 8U;
    for (; i <= 255U; i++) literal_lengths[i] = 9U;
    for (; i <= 279U; i++) literal_lengths[i] = 7U;
    for (; i <= 287U; i++) literal_lengths[i] = 8U;
    for (i = 0U; i < 32U; i++) distance_lengths[i] = 5U;
    return build_huffman(literal, literal_lengths, 288U) &&
           build_huffman(distance, distance_lengths, 32U);
}

static bool dynamic_tables(bit_reader_t *reader, huffman_t *literal,
                           huffman_t *distance) {
    static const uint8_t order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    uint8_t code_lengths[19];
    uint8_t lengths[320];
    huffman_t code_table;
    uint32_t hlit, hdist, hclen, value, index = 0U;
    uint32_t total;
    memset(code_lengths, 0, sizeof(code_lengths));
    if (!read_bits(reader, 5U, &hlit) ||
        !read_bits(reader, 5U, &hdist) ||
        !read_bits(reader, 4U, &hclen)) return false;
    hlit += 257U;
    hdist += 1U;
    hclen += 4U;
    if (hlit > 286U || hdist > 32U) return false;
    for (value = 0U; value < hclen; value++) {
        uint32_t length;
        if (!read_bits(reader, 3U, &length)) return false;
        code_lengths[order[value]] = (uint8_t)length;
    }
    if (!build_huffman(&code_table, code_lengths, 19U)) return false;
    total = hlit + hdist;
    while (index < total) {
        int symbol = decode_symbol(reader, &code_table);
        uint32_t repeat;
        uint8_t fill;
        if (symbol < 0) return false;
        if (symbol <= 15) {
            lengths[index++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16) {
            if (index == 0U || !read_bits(reader, 2U, &repeat)) return false;
            repeat += 3U;
            fill = lengths[index - 1U];
        } else if (symbol == 17) {
            if (!read_bits(reader, 3U, &repeat)) return false;
            repeat += 3U;
            fill = 0U;
        } else if (symbol == 18) {
            if (!read_bits(reader, 7U, &repeat)) return false;
            repeat += 11U;
            fill = 0U;
        } else return false;
        if (repeat > total - index) return false;
        while (repeat--) lengths[index++] = fill;
    }
    return build_huffman(literal, lengths, hlit) &&
           build_huffman(distance, lengths + hlit, hdist);
}

static bool inflate_codes(bit_reader_t *reader, inflate_output_t *output,
                          const huffman_t *literal,
                          const huffman_t *distance) {
    static const uint16_t length_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
        67,83,99,115,131,163,195,227,258
    };
    static const uint8_t length_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,
        4,4,4,4,5,5,5,5,0
    };
    static const uint16_t distance_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,
        8193,12289,16385,24577
    };
    static const uint8_t distance_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    for (;;) {
        int symbol = decode_symbol(reader, literal);
        uint32_t extra, length, dist_symbol, dist;
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (!output_byte(output, (uint8_t)symbol)) return false;
            continue;
        }
        if (symbol == 256) return true;
        if (symbol < 257 || symbol > 285) return false;
        symbol -= 257;
        length = length_base[symbol];
        if (length_extra[symbol]) {
            if (!read_bits(reader, length_extra[symbol], &extra)) return false;
            length += extra;
        }
        symbol = decode_symbol(reader, distance);
        if (symbol < 0 || symbol > 29) return false;
        dist_symbol = (uint32_t)symbol;
        dist = distance_base[dist_symbol];
        if (distance_extra[dist_symbol]) {
            if (!read_bits(reader, distance_extra[dist_symbol], &extra))
                return false;
            dist += extra;
        }
        if (!copy_distance(output, dist, length)) return false;
    }
}

int32_t inflate_raw(const void *input, uint32_t input_length,
                    void *output_data, uint32_t output_capacity) {
    bit_reader_t reader;
    inflate_output_t output;
    bool final_block = false;
    if (!input || !output_data || !output_capacity) return -1;
    reader.data = (const uint8_t *)input;
    reader.length = input_length;
    reader.position = 0U;
    reader.bits = 0U;
    reader.bit_count = 0U;
    output.data = (uint8_t *)output_data;
    output.capacity = output_capacity;
    output.length = 0U;
    while (!final_block) {
        uint32_t final_value, type;
        huffman_t literal, distance;
        if (!read_bits(&reader, 1U, &final_value) ||
            !read_bits(&reader, 2U, &type)) return -1;
        final_block = final_value != 0U;
        if (type == 0U) {
            uint32_t length, inverse, i;
            align_byte(&reader);
            if (reader.position + 4U > reader.length) return -1;
            length = (uint32_t)reader.data[reader.position] |
                     ((uint32_t)reader.data[reader.position + 1U] << 8U);
            inverse = (uint32_t)reader.data[reader.position + 2U] |
                      ((uint32_t)reader.data[reader.position + 3U] << 8U);
            reader.position += 4U;
            if (((length ^ 0xFFFFU) & 0xFFFFU) != inverse ||
                length > reader.length - reader.position ||
                length > output.capacity - output.length) return -1;
            for (i = 0U; i < length; i++)
                output.data[output.length++] = reader.data[reader.position++];
        } else if (type == 1U) {
            if (!fixed_tables(&literal, &distance) ||
                !inflate_codes(&reader, &output, &literal, &distance))
                return -1;
        } else if (type == 2U) {
            if (!dynamic_tables(&reader, &literal, &distance) ||
                !inflate_codes(&reader, &output, &literal, &distance))
                return -1;
        } else return -1;
    }
    return (int32_t)output.length;
}

int32_t inflate_zlib(const void *input, uint32_t input_length,
                     void *output, uint32_t output_capacity) {
    const uint8_t *bytes = (const uint8_t *)input;
    uint32_t offset = 2U;
    if (!bytes || input_length < 6U) return -1;
    if ((bytes[0] & 0x0FU) != 8U ||
        (((uint32_t)bytes[0] << 8U) + bytes[1]) % 31U != 0U) return -1;
    if (bytes[1] & 0x20U) {
        if (input_length < 10U) return -1;
        offset += 4U;
    }
    if (offset + 4U > input_length) return -1;
    return inflate_raw(bytes + offset, input_length - offset - 4U,
                       output, output_capacity);
}

int32_t inflate_gzip(const void *input, uint32_t input_length,
                     void *output, uint32_t output_capacity) {
    const uint8_t *bytes = (const uint8_t *)input;
    uint32_t offset = 10U;
    uint8_t flags;
    if (!bytes || input_length < 18U || bytes[0] != 0x1FU ||
        bytes[1] != 0x8BU || bytes[2] != 8U) return -1;
    flags = bytes[3];
    if (flags & 0x04U) {
        uint32_t extra;
        if (offset + 2U > input_length) return -1;
        extra = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1U] << 8U);
        offset += 2U;
        if (extra > input_length - offset) return -1;
        offset += extra;
    }
    if (flags & 0x08U) {
        while (offset < input_length && bytes[offset]) offset++;
        if (offset++ >= input_length) return -1;
    }
    if (flags & 0x10U) {
        while (offset < input_length && bytes[offset]) offset++;
        if (offset++ >= input_length) return -1;
    }
    if (flags & 0x02U) {
        if (offset + 2U > input_length) return -1;
        offset += 2U;
    }
    if (offset + 8U > input_length) return -1;
    return inflate_raw(bytes + offset, input_length - offset - 8U,
                       output, output_capacity);
}
