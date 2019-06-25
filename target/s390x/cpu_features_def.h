/*
 * CPU features/facilities for s390
 *
 * Copyright IBM Corp. 2016, 2018
 *
 * Author(s): Michael Mueller <mimu@linux.vnet.ibm.com>
 *            David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef TARGET_S390X_CPU_FEATURES_DEF_H
#define TARGET_S390X_CPU_FEATURES_DEF_H

typedef enum {
    /* Stfle */
    S390_FEAT_ESAN3 = 0,
    S390_FEAT_ZARCH,
    S390_FEAT_DAT_ENH,
    S390_FEAT_IDTE_SEGMENT,
    S390_FEAT_IDTE_REGION,
    S390_FEAT_ASN_LX_REUSE,
    S390_FEAT_STFLE,
    S390_FEAT_EDAT,
    S390_FEAT_SENSE_RUNNING_STATUS,
    S390_FEAT_CONDITIONAL_SSKE,
    S390_FEAT_CONFIGURATION_TOPOLOGY,
    S390_FEAT_AP_QUERY_CONFIG_INFO,
    S390_FEAT_IPTE_RANGE,
    S390_FEAT_NONQ_KEY_SETTING,
    S390_FEAT_AP_FACILITIES_TEST,
    S390_FEAT_EXTENDED_TRANSLATION_2,
    S390_FEAT_MSA,
    S390_FEAT_LONG_DISPLACEMENT,
    S390_FEAT_LONG_DISPLACEMENT_FAST,
    S390_FEAT_HFP_MADDSUB,
    S390_FEAT_EXTENDED_IMMEDIATE,
    S390_FEAT_EXTENDED_TRANSLATION_3,
    S390_FEAT_HFP_UNNORMALIZED_EXT,
    S390_FEAT_ETF2_ENH,
    S390_FEAT_STORE_CLOCK_FAST,
    S390_FEAT_PARSING_ENH,
    S390_FEAT_MOVE_WITH_OPTIONAL_SPEC,
    S390_FEAT_TOD_CLOCK_STEERING,
    S390_FEAT_ETF3_ENH,
    S390_FEAT_EXTRACT_CPU_TIME,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE,
    S390_FEAT_COMPARE_AND_SWAP_AND_STORE_2,
    S390_FEAT_GENERAL_INSTRUCTIONS_EXT,
    S390_FEAT_EXECUTE_EXT,
    S390_FEAT_ENHANCED_MONITOR,
    S390_FEAT_FLOATING_POINT_EXT,
    S390_FEAT_ORDER_PRESERVING_COMPRESSION,
    S390_FEAT_SET_PROGRAM_PARAMETERS,
    S390_FEAT_FLOATING_POINT_SUPPPORT_ENH,
    S390_FEAT_DFP,
    S390_FEAT_DFP_FAST,
    S390_FEAT_PFPO,
    S390_FEAT_STFLE_45,
    S390_FEAT_CMPSC_ENH,
    S390_FEAT_DFP_ZONED_CONVERSION,
    S390_FEAT_STFLE_49,
    S390_FEAT_CONSTRAINT_TRANSACTIONAL_EXE,
    S390_FEAT_LOCAL_TLB_CLEARING,
    S390_FEAT_INTERLOCKED_ACCESS_2,
    S390_FEAT_STFLE_53,
    S390_FEAT_ENTROPY_ENC_COMP,
    S390_FEAT_MSA_EXT_5,
    S390_FEAT_MISC_INSTRUCTION_EXT,
    S390_FEAT_SEMAPHORE_ASSIST,
    S390_FEAT_TIME_SLICE_INSTRUMENTATION,
    S390_FEAT_MISC_INSTRUCTION_EXT3,
    S390_FEAT_RUNTIME_INSTRUMENTATION,
    S390_FEAT_ZPCI,
    S390_FEAT_ADAPTER_EVENT_NOTIFICATION,
    S390_FEAT_ADAPTER_INT_SUPPRESSION,
    S390_FEAT_TRANSACTIONAL_EXE,
    S390_FEAT_STORE_HYPERVISOR_INFO,
    S390_FEAT_ACCESS_EXCEPTION_FS_INDICATION,
    S390_FEAT_MSA_EXT_3,
    S390_FEAT_MSA_EXT_4,
    S390_FEAT_EDAT_2,
    S390_FEAT_DFP_PACKED_CONVERSION,
    S390_FEAT_PPA15,
    S390_FEAT_BPB,
    S390_FEAT_VECTOR,
    S390_FEAT_INSTRUCTION_EXEC_PROT,
    S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2,
    S390_FEAT_GUARDED_STORAGE,
    S390_FEAT_VECTOR_PACKED_DECIMAL,
    S390_FEAT_VECTOR_ENH,
    S390_FEAT_MULTIPLE_EPOCH,
    S390_FEAT_TEST_PENDING_EXT_INTERRUPTION,
    S390_FEAT_INSERT_REFERENCE_BITS_MULT,
    S390_FEAT_MSA_EXT_8,
    S390_FEAT_CMM_NT,
    S390_FEAT_VECTOR_ENH2,
    S390_FEAT_ESORT_BASE,
    S390_FEAT_DEFLATE_BASE,
    S390_FEAT_VECTOR_BCD_ENH,
    S390_FEAT_MSA_EXT_9,
    S390_FEAT_ETOKEN,

    /* Sclp Conf Char */
    S390_FEAT_SIE_GSLS,
    S390_FEAT_ESOP,
    S390_FEAT_HPMA2,
    S390_FEAT_SIE_KSS,

    /* Sclp Conf Char Ext */
    S390_FEAT_SIE_64BSCAO,
    S390_FEAT_SIE_CMMA,
    S390_FEAT_SIE_PFMFI,
    S390_FEAT_SIE_IBS,

    /* Sclp Byte 134 */
    S390_FEAT_DIAG318,

    /* Sclp Cpu */
    S390_FEAT_SIE_F2,
    S390_FEAT_SIE_SKEY,
    S390_FEAT_SIE_GPERE,
    S390_FEAT_SIE_SIIF,
    S390_FEAT_SIE_SIGPIF,
    S390_FEAT_SIE_IB,
    S390_FEAT_SIE_CEI,

    /* Misc */
    S390_FEAT_DAT_ENH_2,
    S390_FEAT_CMM,
    S390_FEAT_AP,

    /* PLO */
    S390_FEAT_PLO_CL,
    S390_FEAT_PLO_CLG,
    S390_FEAT_PLO_CLGR,
    S390_FEAT_PLO_CLX,
    S390_FEAT_PLO_CS,
    S390_FEAT_PLO_CSG,
    S390_FEAT_PLO_CSGR,
    S390_FEAT_PLO_CSX,
    S390_FEAT_PLO_DCS,
    S390_FEAT_PLO_DCSG,
    S390_FEAT_PLO_DCSGR,
    S390_FEAT_PLO_DCSX,
    S390_FEAT_PLO_CSST,
    S390_FEAT_PLO_CSSTG,
    S390_FEAT_PLO_CSSTGR,
    S390_FEAT_PLO_CSSTX,
    S390_FEAT_PLO_CSDST,
    S390_FEAT_PLO_CSDSTG,
    S390_FEAT_PLO_CSDSTGR,
    S390_FEAT_PLO_CSDSTX,
    S390_FEAT_PLO_CSTST,
    S390_FEAT_PLO_CSTSTG,
    S390_FEAT_PLO_CSTSTGR,
    S390_FEAT_PLO_CSTSTX,

    /* PTFF */
    S390_FEAT_PTFF_QTO,
    S390_FEAT_PTFF_QSI,
    S390_FEAT_PTFF_QPT,
    S390_FEAT_PTFF_QUI,
    S390_FEAT_PTFF_QTOU,
    S390_FEAT_PTFF_QSIE,
    S390_FEAT_PTFF_QTOUE,
    S390_FEAT_PTFF_STO,
    S390_FEAT_PTFF_STOU,
    S390_FEAT_PTFF_STOE,
    S390_FEAT_PTFF_STOUE,

    /* KMAC */
    S390_FEAT_KMAC_DEA,
    S390_FEAT_KMAC_TDEA_128,
    S390_FEAT_KMAC_TDEA_192,
    S390_FEAT_KMAC_EDEA,
    S390_FEAT_KMAC_ETDEA_128,
    S390_FEAT_KMAC_ETDEA_192,
    S390_FEAT_KMAC_AES_128,
    S390_FEAT_KMAC_AES_192,
    S390_FEAT_KMAC_AES_256,
    S390_FEAT_KMAC_EAES_128,
    S390_FEAT_KMAC_EAES_192,
    S390_FEAT_KMAC_EAES_256,

    /* KMC */
    S390_FEAT_KMC_DEA,
    S390_FEAT_KMC_TDEA_128,
    S390_FEAT_KMC_TDEA_192,
    S390_FEAT_KMC_EDEA,
    S390_FEAT_KMC_ETDEA_128,
    S390_FEAT_KMC_ETDEA_192,
    S390_FEAT_KMC_AES_128,
    S390_FEAT_KMC_AES_192,
    S390_FEAT_KMC_AES_256,
    S390_FEAT_KMC_EAES_128,
    S390_FEAT_KMC_EAES_192,
    S390_FEAT_KMC_EAES_256,
    S390_FEAT_KMC_PRNG,

    /* KM */
    S390_FEAT_KM_DEA,
    S390_FEAT_KM_TDEA_128,
    S390_FEAT_KM_TDEA_192,
    S390_FEAT_KM_EDEA,
    S390_FEAT_KM_ETDEA_128,
    S390_FEAT_KM_ETDEA_192,
    S390_FEAT_KM_AES_128,
    S390_FEAT_KM_AES_192,
    S390_FEAT_KM_AES_256,
    S390_FEAT_KM_EAES_128,
    S390_FEAT_KM_EAES_192,
    S390_FEAT_KM_EAES_256,
    S390_FEAT_KM_XTS_AES_128,
    S390_FEAT_KM_XTS_AES_256,
    S390_FEAT_KM_XTS_EAES_128,
    S390_FEAT_KM_XTS_EAES_256,

    /* KIMD */
    S390_FEAT_KIMD_SHA_1,
    S390_FEAT_KIMD_SHA_256,
    S390_FEAT_KIMD_SHA_512,
    S390_FEAT_KIMD_SHA3_224,
    S390_FEAT_KIMD_SHA3_256,
    S390_FEAT_KIMD_SHA3_384,
    S390_FEAT_KIMD_SHA3_512,
    S390_FEAT_KIMD_SHAKE_128,
    S390_FEAT_KIMD_SHAKE_256,
    S390_FEAT_KIMD_GHASH,

    /* KLMD */
    S390_FEAT_KLMD_SHA_1,
    S390_FEAT_KLMD_SHA_256,
    S390_FEAT_KLMD_SHA_512,
    S390_FEAT_KLMD_SHA3_224,
    S390_FEAT_KLMD_SHA3_256,
    S390_FEAT_KLMD_SHA3_384,
    S390_FEAT_KLMD_SHA3_512,
    S390_FEAT_KLMD_SHAKE_128,
    S390_FEAT_KLMD_SHAKE_256,

    /* PCKMO */
    S390_FEAT_PCKMO_EDEA,
    S390_FEAT_PCKMO_ETDEA_128,
    S390_FEAT_PCKMO_ETDEA_256,
    S390_FEAT_PCKMO_AES_128,
    S390_FEAT_PCKMO_AES_192,
    S390_FEAT_PCKMO_AES_256,
    S390_FEAT_PCKMO_ECC_P256,
    S390_FEAT_PCKMO_ECC_P384,
    S390_FEAT_PCKMO_ECC_P521,
    S390_FEAT_PCKMO_ECC_ED25519,
    S390_FEAT_PCKMO_ECC_ED448,

    /* KMCTR */
    S390_FEAT_KMCTR_DEA,
    S390_FEAT_KMCTR_TDEA_128,
    S390_FEAT_KMCTR_TDEA_192,
    S390_FEAT_KMCTR_EDEA,
    S390_FEAT_KMCTR_ETDEA_128,
    S390_FEAT_KMCTR_ETDEA_192,
    S390_FEAT_KMCTR_AES_128,
    S390_FEAT_KMCTR_AES_192,
    S390_FEAT_KMCTR_AES_256,
    S390_FEAT_KMCTR_EAES_128,
    S390_FEAT_KMCTR_EAES_192,
    S390_FEAT_KMCTR_EAES_256,

    /* KMF */
    S390_FEAT_KMF_DEA,
    S390_FEAT_KMF_TDEA_128,
    S390_FEAT_KMF_TDEA_192,
    S390_FEAT_KMF_EDEA,
    S390_FEAT_KMF_ETDEA_128,
    S390_FEAT_KMF_ETDEA_192,
    S390_FEAT_KMF_AES_128,
    S390_FEAT_KMF_AES_192,
    S390_FEAT_KMF_AES_256,
    S390_FEAT_KMF_EAES_128,
    S390_FEAT_KMF_EAES_192,
    S390_FEAT_KMF_EAES_256,

    /* KMO */
    S390_FEAT_KMO_DEA,
    S390_FEAT_KMO_TDEA_128,
    S390_FEAT_KMO_TDEA_192,
    S390_FEAT_KMO_EDEA,
    S390_FEAT_KMO_ETDEA_128,
    S390_FEAT_KMO_ETDEA_192,
    S390_FEAT_KMO_AES_128,
    S390_FEAT_KMO_AES_192,
    S390_FEAT_KMO_AES_256,
    S390_FEAT_KMO_EAES_128,
    S390_FEAT_KMO_EAES_192,
    S390_FEAT_KMO_EAES_256,

    /* PCC */
    S390_FEAT_PCC_CMAC_DEA,
    S390_FEAT_PCC_CMAC_TDEA_128,
    S390_FEAT_PCC_CMAC_TDEA_192,
    S390_FEAT_PCC_CMAC_ETDEA_128,
    S390_FEAT_PCC_CMAC_ETDEA_192,
    S390_FEAT_PCC_CMAC_TDEA,
    S390_FEAT_PCC_CMAC_AES_128,
    S390_FEAT_PCC_CMAC_AES_192,
    S390_FEAT_PCC_CMAC_AES_256,
    S390_FEAT_PCC_CMAC_EAES_128,
    S390_FEAT_PCC_CMAC_EAES_192,
    S390_FEAT_PCC_CMAC_EAES_256,
    S390_FEAT_PCC_XTS_AES_128,
    S390_FEAT_PCC_XTS_AES_256,
    S390_FEAT_PCC_XTS_EAES_128,
    S390_FEAT_PCC_XTS_EAES_256,
    S390_FEAT_PCC_SCALAR_MULT_P256,
    S390_FEAT_PCC_SCALAR_MULT_P384,
    S390_FEAT_PCC_SCALAR_MULT_P512,
    S390_FEAT_PCC_SCALAR_MULT_ED25519,
    S390_FEAT_PCC_SCALAR_MULT_ED448,
    S390_FEAT_PCC_SCALAR_MULT_X25519,
    S390_FEAT_PCC_SCALAR_MULT_X448,

    /* PPNO/PRNO */
    S390_FEAT_PPNO_SHA_512_DRNG,
    S390_FEAT_PRNO_TRNG_QRTCR,
    S390_FEAT_PRNO_TRNG,

    /* KMA */
    S390_FEAT_KMA_GCM_AES_128,
    S390_FEAT_KMA_GCM_AES_192,
    S390_FEAT_KMA_GCM_AES_256 ,
    S390_FEAT_KMA_GCM_EAES_128,
    S390_FEAT_KMA_GCM_EAES_192,
    S390_FEAT_KMA_GCM_EAES_256,

    /* KDSA */
    S390_FEAT_ECDSA_VERIFY_P256,
    S390_FEAT_ECDSA_VERIFY_P384,
    S390_FEAT_ECDSA_VERIFY_P512,
    S390_FEAT_ECDSA_SIGN_P256,
    S390_FEAT_ECDSA_SIGN_P384,
    S390_FEAT_ECDSA_SIGN_P512,
    S390_FEAT_EECDSA_SIGN_P256,
    S390_FEAT_EECDSA_SIGN_P384,
    S390_FEAT_EECDSA_SIGN_P512,
    S390_FEAT_EDDSA_VERIFY_ED25519,
    S390_FEAT_EDDSA_VERIFY_ED448,
    S390_FEAT_EDDSA_SIGN_ED25519,
    S390_FEAT_EDDSA_SIGN_ED448,
    S390_FEAT_EEDDSA_SIGN_ED25519,
    S390_FEAT_EEDDSA_SIGN_ED448,

    /* SORTL */
    S390_FEAT_SORTL_SFLR,
    S390_FEAT_SORTL_SVLR,
    S390_FEAT_SORTL_32,
    S390_FEAT_SORTL_128,
    S390_FEAT_SORTL_F0,

    /* DEFLATE */
    S390_FEAT_DEFLATE_GHDT,
    S390_FEAT_DEFLATE_CMPR,
    S390_FEAT_DEFLATE_XPND,
    S390_FEAT_DEFLATE_F0,

    S390_FEAT_MAX,
} S390Feat;

#endif /* TARGET_S390X_CPU_FEATURES_DEF_H */
