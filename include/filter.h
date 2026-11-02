#ifndef ATG_ENGINE_SIM_FILTER_H
#define ATG_ENGINE_SIM_FILTER_H

#ifdef _MSC_VER
#define ATG_FORCE_INLINE __forceinline
#else
#define ATG_FORCE_INLINE inline __attribute__((always_inline))
#endif

class Filter {
    public:
        Filter();
        virtual ~Filter();

        virtual float f(float sample);
        virtual void destroy();
};

#endif /* ATG_ENGINE_SIM_FILTER_H */
