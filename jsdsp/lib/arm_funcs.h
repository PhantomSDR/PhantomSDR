#include <stdint.h>
#include <string.h>
#define float32_t float

typedef struct
{
        uint16_t numTaps;     /**< number of filter coefficients in the filter. */
        float32_t *pState;    /**< points to the state variable array. The array is of length numTaps+blockSize-1. */
const float32_t *pCoeffs;   /**< points to the coefficient array. The array is of length numTaps. */
} arm_fir_instance_f32;

void arm_fir_init_f32(
        arm_fir_instance_f32 * S,
        uint16_t numTaps,
  const float32_t * pCoeffs,
        float32_t * pState,
        uint32_t blockSize)
{
  /* Assign filter taps */
  S->numTaps = numTaps;

  /* Assign coefficient pointer */
  S->pCoeffs = pCoeffs;

  /* Clear state buffer. The size is always (blockSize + numTaps - 1) */
#if defined(ARM_MATH_MVEF) && !defined(ARM_MATH_AUTOVECTORIZE)
  memset(pState, 0, (numTaps + (blockSize - 1U) + blockSize) * sizeof(float32_t));
#else
  memset(pState, 0, (numTaps + (blockSize - 1U)) * sizeof(float32_t));
#endif
  /* Assign state pointer */
  S->pState = pState;
}


void arm_fir_f32(
  const arm_fir_instance_f32 * S,
  const float32_t * pSrc,
        float32_t * pDst,
        uint32_t blockSize)
{
        float32_t *pState = S->pState;                 /* State pointer */
  const float32_t *pCoeffs = S->pCoeffs;               /* Coefficient pointer */
        float32_t *pStateCurnt;                        /* Points to the current sample of the state */
        float32_t *px;                                 /* Temporary pointer for state buffer */
  const float32_t *pb;                                 /* Temporary pointer for coefficient buffer */
        float32_t acc0;                                /* Accumulator */
        uint32_t numTaps = S->numTaps;                 /* Number of filter coefficients in the filter */
        uint32_t i, tapCnt, blkCnt;                    /* Loop counters */

#if defined (ARM_MATH_LOOPUNROLL)
        float32_t acc1, acc2, acc3, acc4, acc5, acc6, acc7;     /* Accumulators */
        float32_t x0, x1, x2, x3, x4, x5, x6, x7;               /* Temporary variables to hold state values */
        float32_t c0;                                           /* Temporary variable to hold coefficient value */
#endif

  /* S->pState points to state array which contains previous frame (numTaps - 1) samples */
  /* pStateCurnt points to the location where the new input data should be written */
  pStateCurnt = &(S->pState[(numTaps - 1U)]);

#if defined (ARM_MATH_LOOPUNROLL)

  /* Loop unrolling: Compute 8 output values simultaneously.
   * The variables acc0 ... acc7 hold output values that are being computed:
   *
   *    acc0 =  b[numTaps-1] * x[n-numTaps-1] + b[numTaps-2] * x[n-numTaps-2] + b[numTaps-3] * x[n-numTaps-3] +...+ b[0] * x[0]
   *    acc1 =  b[numTaps-1] * x[n-numTaps]   + b[numTaps-2] * x[n-numTaps-1] + b[numTaps-3] * x[n-numTaps-2] +...+ b[0] * x[1]
   *    acc2 =  b[numTaps-1] * x[n-numTaps+1] + b[numTaps-2] * x[n-numTaps]   + b[numTaps-3] * x[n-numTaps-1] +...+ b[0] * x[2]
   *    acc3 =  b[numTaps-1] * x[n-numTaps+2] + b[numTaps-2] * x[n-numTaps+1] + b[numTaps-3] * x[n-numTaps]   +...+ b[0] * x[3]
   */

  blkCnt = blockSize >> 3U;

  while (blkCnt > 0U)
  {
    /* Copy 4 new input samples into the state buffer. */
    *pStateCurnt++ = *pSrc++;
    *pStateCurnt++ = *pSrc++;
    *pStateCurnt++ = *pSrc++;
    *pStateCurnt++ = *pSrc++;

    /* Set all accumulators to zero */
    acc0 = 0.0f;
    acc1 = 0.0f;
    acc2 = 0.0f;
    acc3 = 0.0f;
    acc4 = 0.0f;
    acc5 = 0.0f;
    acc6 = 0.0f;
    acc7 = 0.0f;

    /* Initialize state pointer */
    px = pState;

    /* Initialize coefficient pointer */
    pb = pCoeffs;

    /* This is separated from the others to avoid
     * a call to __aeabi_memmove which would be slower
     */
    *pStateCurnt++ = *pSrc++;
    *pStateCurnt++ = *pSrc++;
    *pStateCurnt++ = *pSrc++;
    *pStateCurnt++ = *pSrc++;

    /* Read the first 7 samples from the state buffer:  x[n-numTaps], x[n-numTaps-1], x[n-numTaps-2] */
    x0 = *px++;
    x1 = *px++;
    x2 = *px++;
    x3 = *px++;
    x4 = *px++;
    x5 = *px++;
    x6 = *px++;

    /* Loop unrolling: process 8 taps at a time. */
    tapCnt = numTaps >> 3U;

    while (tapCnt > 0U)
    {
      /* Read the b[numTaps-1] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-3] sample */
      x7 = *(px++);

      /* acc0 +=  b[numTaps-1] * x[n-numTaps] */
      acc0 += x0 * c0;

      /* acc1 +=  b[numTaps-1] * x[n-numTaps-1] */
      acc1 += x1 * c0;

      /* acc2 +=  b[numTaps-1] * x[n-numTaps-2] */
      acc2 += x2 * c0;

      /* acc3 +=  b[numTaps-1] * x[n-numTaps-3] */
      acc3 += x3 * c0;

      /* acc4 +=  b[numTaps-1] * x[n-numTaps-4] */
      acc4 += x4 * c0;

      /* acc1 +=  b[numTaps-1] * x[n-numTaps-5] */
      acc5 += x5 * c0;

      /* acc2 +=  b[numTaps-1] * x[n-numTaps-6] */
      acc6 += x6 * c0;

      /* acc3 +=  b[numTaps-1] * x[n-numTaps-7] */
      acc7 += x7 * c0;

      /* Read the b[numTaps-2] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-4] sample */
      x0 = *(px++);

      /* Perform the multiply-accumulate */
      acc0 += x1 * c0;
      acc1 += x2 * c0;
      acc2 += x3 * c0;
      acc3 += x4 * c0;
      acc4 += x5 * c0;
      acc5 += x6 * c0;
      acc6 += x7 * c0;
      acc7 += x0 * c0;

      /* Read the b[numTaps-3] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-5] sample */
      x1 = *(px++);

      /* Perform the multiply-accumulates */
      acc0 += x2 * c0;
      acc1 += x3 * c0;
      acc2 += x4 * c0;
      acc3 += x5 * c0;
      acc4 += x6 * c0;
      acc5 += x7 * c0;
      acc6 += x0 * c0;
      acc7 += x1 * c0;

      /* Read the b[numTaps-4] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-6] sample */
      x2 = *(px++);

      /* Perform the multiply-accumulates */
      acc0 += x3 * c0;
      acc1 += x4 * c0;
      acc2 += x5 * c0;
      acc3 += x6 * c0;
      acc4 += x7 * c0;
      acc5 += x0 * c0;
      acc6 += x1 * c0;
      acc7 += x2 * c0;

      /* Read the b[numTaps-4] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-6] sample */
      x3 = *(px++);
      /* Perform the multiply-accumulates */
      acc0 += x4 * c0;
      acc1 += x5 * c0;
      acc2 += x6 * c0;
      acc3 += x7 * c0;
      acc4 += x0 * c0;
      acc5 += x1 * c0;
      acc6 += x2 * c0;
      acc7 += x3 * c0;

      /* Read the b[numTaps-4] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-6] sample */
      x4 = *(px++);

      /* Perform the multiply-accumulates */
      acc0 += x5 * c0;
      acc1 += x6 * c0;
      acc2 += x7 * c0;
      acc3 += x0 * c0;
      acc4 += x1 * c0;
      acc5 += x2 * c0;
      acc6 += x3 * c0;
      acc7 += x4 * c0;

      /* Read the b[numTaps-4] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-6] sample */
      x5 = *(px++);

      /* Perform the multiply-accumulates */
      acc0 += x6 * c0;
      acc1 += x7 * c0;
      acc2 += x0 * c0;
      acc3 += x1 * c0;
      acc4 += x2 * c0;
      acc5 += x3 * c0;
      acc6 += x4 * c0;
      acc7 += x5 * c0;

      /* Read the b[numTaps-4] coefficient */
      c0 = *(pb++);

      /* Read x[n-numTaps-6] sample */
      x6 = *(px++);

      /* Perform the multiply-accumulates */
      acc0 += x7 * c0;
      acc1 += x0 * c0;
      acc2 += x1 * c0;
      acc3 += x2 * c0;
      acc4 += x3 * c0;
      acc5 += x4 * c0;
      acc6 += x5 * c0;
      acc7 += x6 * c0;

      /* Decrement loop counter */
      tapCnt--;
    }

    /* Loop unrolling: Compute remaining outputs */
    tapCnt = numTaps % 0x8U;

    while (tapCnt > 0U)
    {
      /* Read coefficients */
      c0 = *(pb++);

      /* Fetch 1 state variable */
      x7 = *(px++);

      /* Perform the multiply-accumulates */
      acc0 += x0 * c0;
      acc1 += x1 * c0;
      acc2 += x2 * c0;
      acc3 += x3 * c0;
      acc4 += x4 * c0;
      acc5 += x5 * c0;
      acc6 += x6 * c0;
      acc7 += x7 * c0;

      /* Reuse the present sample states for next sample */
      x0 = x1;
      x1 = x2;
      x2 = x3;
      x3 = x4;
      x4 = x5;
      x5 = x6;
      x6 = x7;

      /* Decrement loop counter */
      tapCnt--;
    }

    /* Advance the state pointer by 8 to process the next group of 8 samples */
    pState = pState + 8;

    /* The results in the 8 accumulators, store in the destination buffer. */
    *pDst++ = acc0;
    *pDst++ = acc1;
    *pDst++ = acc2;
    *pDst++ = acc3;
    *pDst++ = acc4;
    *pDst++ = acc5;
    *pDst++ = acc6;
    *pDst++ = acc7;


    /* Decrement loop counter */
    blkCnt--;
  }

  /* Loop unrolling: Compute remaining output samples */
  blkCnt = blockSize % 0x8U;

#else

  /* Initialize blkCnt with number of taps */
  blkCnt = blockSize;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

  while (blkCnt > 0U)
  {
    /* Copy one sample at a time into state buffer */
    *pStateCurnt++ = *pSrc++;

    /* Set the accumulator to zero */
    acc0 = 0.0f;

    /* Initialize state pointer */
    px = pState;

    /* Initialize Coefficient pointer */
    pb = pCoeffs;

    i = numTaps;

    /* Perform the multiply-accumulates */
    while (i > 0U)
    {
      /* acc =  b[numTaps-1] * x[n-numTaps-1] + b[numTaps-2] * x[n-numTaps-2] + b[numTaps-3] * x[n-numTaps-3] +...+ b[0] * x[0] */
      acc0 += *px++ * *pb++;

      i--;
    }

    /* Store result in destination buffer. */
    *pDst++ = acc0;

    /* Advance state pointer by 1 for the next sample */
    pState = pState + 1U;

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Processing is complete.
     Now copy the last numTaps - 1 samples to the start of the state buffer.
     This prepares the state buffer for the next function call. */

  /* Points to the start of the state buffer */
  pStateCurnt = S->pState;

#if defined (ARM_MATH_LOOPUNROLL)

  /* Loop unrolling: Compute 4 taps at a time */
  tapCnt = (numTaps - 1U) >> 2U;

  /* Copy data */
  while (tapCnt > 0U)
  {
    *pStateCurnt++ = *pState++;
    *pStateCurnt++ = *pState++;
    *pStateCurnt++ = *pState++;
    *pStateCurnt++ = *pState++;

    /* Decrement loop counter */
    tapCnt--;
  }

  /* Calculate remaining number of copies */
  tapCnt = (numTaps - 1U) % 0x4U;

#else

  /* Initialize tapCnt with number of taps */
  tapCnt = (numTaps - 1U);

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

  /* Copy remaining data */
  while (tapCnt > 0U)
  {
    *pStateCurnt++ = *pState++;

    /* Decrement loop counter */
    tapCnt--;
  }

}


void arm_negate_f32(float* src, float* dst, size_t size) {
    for(int i = 0;i < size;i++) {
        dst[i] = -src[i];
    }
}

void arm_dot_prod_f32(float* a, float* b, size_t size, float* c) {
    float sum = 0;
    for(int i = 0;i < size;i++) {
        sum += a[i] * b[i];
    }
    *c = sum;
}
void arm_var_f32(
  const float32_t * pSrc,
        uint32_t blockSize,
        float32_t * pResult)
{
        uint32_t blkCnt;                               /* Loop counter */
        float32_t sum = 0.0f;                          /* Temporary result storage */
        float32_t fSum = 0.0f;
        float32_t fMean, fValue;
  const float32_t * pInput = pSrc;

  if (blockSize <= 1U)
  {
    *pResult = 0;
    return;
  }

#if defined (ARM_MATH_LOOPUNROLL) && !defined(ARM_MATH_AUTOVECTORIZE)

  /* Loop unrolling: Compute 4 outputs at a time */
  blkCnt = blockSize >> 2U;

  while (blkCnt > 0U)
  {
    /* C = (A[0] + A[1] + A[2] + ... + A[blockSize-1]) */

    sum += *pInput++;
    sum += *pInput++;
    sum += *pInput++;
    sum += *pInput++;


    /* Decrement loop counter */
    blkCnt--;
  }

  /* Loop unrolling: Compute remaining outputs */
  blkCnt = blockSize % 0x4U;

#else

  /* Initialize blkCnt with number of samples */
  blkCnt = blockSize;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

  while (blkCnt > 0U)
  {
    /* C = (A[0] + A[1] + A[2] + ... + A[blockSize-1]) */

    sum += *pInput++;

    /* Decrement loop counter */
    blkCnt--;
  }

  /* C = (A[0] + A[1] + A[2] + ... + A[blockSize-1]) / blockSize  */
  fMean = sum / (float32_t) blockSize;

  pInput = pSrc;

#if defined (ARM_MATH_LOOPUNROLL) && !defined(ARM_MATH_AUTOVECTORIZE)

  /* Loop unrolling: Compute 4 outputs at a time */
  blkCnt = blockSize >> 2U;

  while (blkCnt > 0U)
  {
    fValue = *pInput++ - fMean;
    fSum += fValue * fValue;

    fValue = *pInput++ - fMean;
    fSum += fValue * fValue;

    fValue = *pInput++ - fMean;
    fSum += fValue * fValue;

    fValue = *pInput++ - fMean;
    fSum += fValue * fValue;

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Loop unrolling: Compute remaining outputs */
  blkCnt = blockSize % 0x4U;

#else

  /* Initialize blkCnt with number of samples */
  blkCnt = blockSize;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

  while (blkCnt > 0U)
  {
    fValue = *pInput++ - fMean;
    fSum += fValue * fValue;

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Variance */
  *pResult = fSum / (float32_t)(blockSize - 1.0f);
}
void arm_power_f32(
  const float32_t * pSrc,
        uint32_t blockSize,
        float32_t * pResult)
{
        uint32_t blkCnt;                               /* Loop counter */
        float32_t sum = 0.0f;                          /* Temporary result storage */
        float32_t in;                                  /* Temporary variable to store input value */

#if defined (ARM_MATH_LOOPUNROLL) && !defined(ARM_MATH_AUTOVECTORIZE)

  /* Loop unrolling: Compute 4 outputs at a time */
  blkCnt = blockSize >> 2U;

  while (blkCnt > 0U)
  {
    /* C = A[0] * A[0] + A[1] * A[1] + ... + A[blockSize-1] * A[blockSize-1] */

    /* Compute Power and store result in a temporary variable, sum. */
    in = *pSrc++;
    sum += in * in;

    in = *pSrc++;
    sum += in * in;

    in = *pSrc++;
    sum += in * in;

    in = *pSrc++;
    sum += in * in;

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Loop unrolling: Compute remaining outputs */
  blkCnt = blockSize % 0x4U;

#else

  /* Initialize blkCnt with number of samples */
  blkCnt = blockSize;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */

  while (blkCnt > 0U)
  {
    /* C = A[0] * A[0] + A[1] * A[1] + ... + A[blockSize-1] * A[blockSize-1] */

    /* Compute Power and store result in a temporary variable, sum. */
    in = *pSrc++;
    sum += in * in;

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Store result to destination */
  *pResult = sum;
}

void arm_mult_f32(
  const float32_t * pSrcA,
  const float32_t * pSrcB,
        float32_t * pDst,
        uint32_t blockSize)
{
    uint32_t blkCnt;                               /* Loop counter */

#if defined(ARM_MATH_NEON) && !defined(ARM_MATH_AUTOVECTORIZE)
    f32x4_t vec1;
    f32x4_t vec2;
    f32x4_t res;

    /* Compute 4 outputs at a time */
    blkCnt = blockSize >> 2U;

    while (blkCnt > 0U)
    {
        /* C = A * B */

    	/* Multiply the inputs and then store the results in the destination buffer. */
        vec1 = vld1q_f32(pSrcA);
        vec2 = vld1q_f32(pSrcB);
        res = vmulq_f32(vec1, vec2);
        vst1q_f32(pDst, res);

        /* Increment pointers */
        pSrcA += 4;
        pSrcB += 4; 
        pDst += 4;
        
        /* Decrement the loop counter */
        blkCnt--;
    }

    /* Tail */
    blkCnt = blockSize & 0x3;

#else
#if defined (ARM_MATH_LOOPUNROLL) && !defined(ARM_MATH_AUTOVECTORIZE)

  /* Loop unrolling: Compute 4 outputs at a time */
  blkCnt = blockSize >> 2U;

  while (blkCnt > 0U)
  {
    /* C = A * B */

    /* Multiply inputs and store result in destination buffer. */
    *pDst++ = (*pSrcA++) * (*pSrcB++);

    *pDst++ = (*pSrcA++) * (*pSrcB++);

    *pDst++ = (*pSrcA++) * (*pSrcB++);

    *pDst++ = (*pSrcA++) * (*pSrcB++);

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Loop unrolling: Compute remaining outputs */
  blkCnt = blockSize % 0x4U;

#else

  /* Initialize blkCnt with number of samples */
  blkCnt = blockSize;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */
#endif /* #if defined(ARM_MATH_NEON) */

  while (blkCnt > 0U)
  {
    /* C = A * B */

    /* Multiply input and store result in destination buffer. */
    *pDst++ = (*pSrcA++) * (*pSrcB++);

    /* Decrement loop counter */
    blkCnt--;
  }

}

void arm_add_f32(
  const float32_t * pSrcA,
  const float32_t * pSrcB,
        float32_t * pDst,
        uint32_t blockSize)
{
        uint32_t blkCnt;                               /* Loop counter */

#if defined(ARM_MATH_NEON) && !defined(ARM_MATH_AUTOVECTORIZE)
    f32x4_t vec1;
    f32x4_t vec2;
    f32x4_t res;

    /* Compute 4 outputs at a time */
    blkCnt = blockSize >> 2U;

    while (blkCnt > 0U)
    {
        /* C = A + B */

    	/* Add and then store the results in the destination buffer. */
        vec1 = vld1q_f32(pSrcA);
        vec2 = vld1q_f32(pSrcB);
        res = vaddq_f32(vec1, vec2);
        vst1q_f32(pDst, res);

        /* Increment pointers */
        pSrcA += 4;
        pSrcB += 4; 
        pDst += 4;
        
        /* Decrement the loop counter */
        blkCnt--;
    }

    /* Tail */
    blkCnt = blockSize & 0x3;

#else
#if defined (ARM_MATH_LOOPUNROLL) && !defined(ARM_MATH_AUTOVECTORIZE)

  /* Loop unrolling: Compute 4 outputs at a time */
  blkCnt = blockSize >> 2U;

  while (blkCnt > 0U)
  {
    /* C = A + B */

    /* Add and store result in destination buffer. */
    *pDst++ = (*pSrcA++) + (*pSrcB++);
    *pDst++ = (*pSrcA++) + (*pSrcB++);
    *pDst++ = (*pSrcA++) + (*pSrcB++);
    *pDst++ = (*pSrcA++) + (*pSrcB++);

    /* Decrement loop counter */
    blkCnt--;
  }

  /* Loop unrolling: Compute remaining outputs */
  blkCnt = blockSize % 0x4U;

#else

  /* Initialize blkCnt with number of samples */
  blkCnt = blockSize;

#endif /* #if defined (ARM_MATH_LOOPUNROLL) */
#endif /* #if defined(ARM_MATH_NEON) */

  while (blkCnt > 0U)
  {
    /* C = A + B */

    /* Add and store result in destination buffer. */
    *pDst++ = (*pSrcA++) + (*pSrcB++);

    /* Decrement loop counter */
    blkCnt--;
  }

}