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

class SampleConverterBase {
  protected:
    std::unique_ptr<SampleReader> reader;

  public:
    SampleConverterBase(std::unique_ptr<SampleReader> reader);
    virtual void read(float *arr, int num) = 0;
    virtual ~SampleConverterBase() {}
};

template <typename T> class SampleConverter : public SampleConverterBase {
  public:
    SampleConverter(std::unique_ptr<SampleReader> reader);
    virtual void read(float *arr, int num);
    virtual ~SampleConverter() {}
};

#endif
