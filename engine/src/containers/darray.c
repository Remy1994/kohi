#include "darray.h"

#include "core/kmemory.h"
#include "core/logger.h"

void* _darray_create(u64 length, u64 stride) {
    u64 header_size = DARRAY_FIELD_LENGTH * sizeof(u64);
    u64 array_size = length * stride;
    u64* new_array = kallocate(header_size + array_size, MEMORY_TAG_DARRAY);
    new_array[DARRAY_CAPACITY] = length;
    new_array[DARRAY_LENGTH] = 0;
    new_array[DARRAY_STRIDE] = stride;
    return (void*)(new_array + DARRAY_FIELD_LENGTH);
}

void _darray_destroy(void* array) {
    u64* header = (u64*)array - DARRAY_FIELD_LENGTH;
    u64 header_size = DARRAY_FIELD_LENGTH * sizeof(u64);
    u64 array_size = header[DARRAY_CAPACITY] * header[DARRAY_STRIDE];
    u64 total_size = header_size + array_size;
    kfree(header, total_size, MEMORY_TAG_DARRAY);
}

u64 _darray_field_get(void* array, u64 field) {
    u64* header = (u64*)array - DARRAY_FIELD_LENGTH;
    return header[field];
}

void _darray_field_set(void* array, u64 field, u64 value) {
    u64* header = (u64*)array - DARRAY_FIELD_LENGTH;
    header[field] = value;
}

void* _darray_resize(void* array) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    void* temp = _darray_create(DARRAY_RESIZE_FACTOR * darray_capacity(array), stride);
    kcopy_memory(temp, array, length * stride);

    _darray_field_set(temp, DARRAY_LENGTH, length);
    _darray_destroy(array);
    return temp;
}

void* _darray_push(void* array, const void* value_ptr) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    if (length >= darray_capacity(array)) {
        array = _darray_resize(array);
    }

    kcopy_memory((char*)array + length * stride, value_ptr, stride);
    _darray_field_set(array, DARRAY_LENGTH, length + 1);
    return array;
}

void _darray_pop(void* array, void* dest) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);

    kcopy_memory(dest, (char*)array + (length - 1) * stride, stride);
    _darray_field_set(array, DARRAY_LENGTH, length - 1);
}

void* _darray_pop_at(void* array, u64 index, void* dest) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    if (index >= length) {
        KERROR("Index outside the bounds of this array! Length: %i, index: %i", length, index);
        return array;
    }

    kcopy_memory(dest, (char*)array + index * stride, stride);
    if (index != length - 1) {
        for (i32 i = 0; i < length - 1 - index; ++i) {
            kcopy_memory(
                (char*)array + (index + i) * stride,
                (char*)array + (index + i + 1) * stride,
                stride);
        }
    }

    _darray_field_set(array, DARRAY_LENGTH, length - 1);
    return array;
}
void* _darray_insert_at(void* array, u64 index, void* value_ptr) {
    u64 length = darray_length(array);
    u64 stride = darray_stride(array);
    if (index > length) {
        KERROR("Index outside the bounds of this array! Length: %i, index: %i", length, index);
        return array;
    }
    if (length >= darray_capacity(array)) {
        array = _darray_resize(array);
    }

    if (index != length) {
        for (i32 i = 0; i < length - index; ++i) {
            kcopy_memory(
                (char*)array + (length - i) * stride,
                (char*)array + (length - i - 1) * stride,
                stride);
        }
    }

    kcopy_memory((char*)array + index * stride, value_ptr, stride);
    _darray_field_set(array, DARRAY_LENGTH, length + 1);

    return array;
}