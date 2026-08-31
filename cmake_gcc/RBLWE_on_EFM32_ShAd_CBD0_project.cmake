target_sources(RBLWE_on_EFM32_ShAd_CBD0 PRIVATE
	"../timing_test.c"
	"../decrypt.c"
	"../encrypt.c"
	"../keygen.c"
	"../masked_decode.c"
	"../ntt.c"
	"../rlwe_cca2_pke.c"
	"../rng_pool.c"
	"../sampling.c"
)
