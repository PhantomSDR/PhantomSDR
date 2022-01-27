#ifndef SAMPLEREADER_H
#define SAMPLEREADER_H

#include <cstdio>
#include <memory>
class SampleReader {
  public:
    virtual int read(void *arr, int num) = 0;
    virtual ~SampleReader() {}
};

class FileSampleReader : public SampleReader {
  protected:
    FILE *f;

  public:
    FileSampleReader(FILE *f);
    int read(void *arr, int num);
};

class SampleConverter {
  protected:
    std::unique_ptr<SampleReader> reader;

  public:
    SampleConverter(std::unique_ptr<SampleReader> reader);
    virtual void read(float *arr, int num) = 0;
    virtual ~SampleConverter() {}
};

class Int8SampleConverter : public SampleConverter {
  public:
    Int8SampleConverter(std::unique_ptr<SampleReader> reader);
    void read(float *arr, int num);
};

class Uint8SampleConverter : public SampleConverter {
  public:
    Uint8SampleConverter(std::unique_ptr<SampleReader> reader);
    void read(float *arr, int num);
};

class Int16SampleConverter : public SampleConverter {
  public:
    Int16SampleConverter(std::unique_ptr<SampleReader> reader);
    void read(float *arr, int num);
};

class Uint16SampleConverter : public SampleConverter {
  public:
    Uint16SampleConverter(std::unique_ptr<SampleReader> reader);
    void read(float *arr, int num);
};

class Float32SampleConverter : public SampleConverter {
  public:
    Float32SampleConverter(std::unique_ptr<SampleReader> reader);
    void read(float *arr, int num);
};

class Float64SampleConverter : public SampleConverter {
  public:
    Float64SampleConverter(std::unique_ptr<SampleReader> reader);
    void read(float *arr, int num);
};

#endif
