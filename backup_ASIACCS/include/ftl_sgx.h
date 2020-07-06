/*
SGX 관련 data 관리하는 파트이다.
//1. Enclave-Public key table
//2. Enclave-File table
//3. File-FTL

1. Name Hash Table (File name to Inode)
2. Inode
3. File table
4. File FTL
*/
#include "jasmine.h"
//#include "ecdsa_sgx.h"


//DS
//#define NUM_FILE (1024)
//#define BUCKET_SIZE (256)
//#define ENC_SIZE (256)
#define NUM_DIRECT_MAP (2)
#define NUM_INDIRECT_MAP (1)
#define NUM_DOUBLE_INDIRECT_MAP (4)
//#define NUM_TRIPLE_INDIRECT_MAP 1
//#define DS_FTL_SIZE (128)    //ECC고려하면 128bytes여야 포인팅이됨.
#define FILE_FTL_NUM (DS_FTL_SIZE / 4)

#define TYPE_INODE 0
#define TYPE_FTL 1
#define TYPE_NHASH 2

#define NAME_LEN 16

#define KEY_SIZE (16) // key size : 128bit 
#define MAC_SIZE (KEY_SIZE+KEY_SIZE)

//Bitmap to allocate / free metadata
//bitmap for inode
//1bit per 36bytes.
//static UINT32 file_table[FILE_SIZE];

#define DS_INODE_BMP_SIZE ((DS_MAX_FILE+7)/8)
#define DS_FTL_BMP_SIZE ((DS_MAX_FTL+7)/8)
#define DS_NHASH_BMP_SIZE ((DS_MAX_NHASH+7)/8)

#define FILE_NOT_EXISTS (-2)

//each ftl chunk has 128 elements 
//each element has one virtual page 64)(32KB here.)
//so We can store 4MB per ftl chunk
typedef struct FILE_FTL{
    UINT32 vpn[FILE_FTL_NUM]; //512/4 = 128개의 elements
}FILE_FTL;
//This is indiect pointer chunk 
//similar to ftl chunk
typedef struct FILE_INDIRECTED{
    FILE_FTL *next_mapped[FILE_FTL_NUM];
}FILE_INDIRECTED;

typedef struct FILE_DOUBLED{
    FILE_INDIRECTED *next_mapped[FILE_FTL_NUM];
}FILE_DOUBLED;


//File FTL. It follows EXT2 data structure : ref: ko.wikipedia.org/wiki/Ext2
//each has mapping table. 
//now, 16bytes for each file -> FILE metadata has 2+4+16 = 22bytes now //can be fixed.
//each ftl chunk has 512bytes(sector size)
typedef struct FILE_FTL_META{
    FILE_FTL *direct_mapped[NUM_DIRECT_MAP];   //4MB * 2 = 8MB
    FILE_INDIRECTED *indirect_mapped[NUM_INDIRECT_MAP];   //4MB * 128 = 512MB
    FILE_DOUBLED *double_indirect_mapped[NUM_DOUBLE_INDIRECT_MAP]; //4MB * 128 * 128 = 65536MB(64GB)
  //  UINT32 triple_indirect_mapped[NUM_TRIPLE_INDIRECT_MAP]; //We don't neeed this.
}FILE_FTL_META;

//크기는 128바이트의 약수여야함.
//왜냐하면 ECC 문제가 생기기 떄문 (DRAM에서 포인터는 Virtual Address다.)
//포인터 DRAM에서 함부러 쓰면 큰일난다.
//지금은 64bytes로 가정하자.
//현재 딱 64MB
typedef struct INODE{
    UINT8 key[KEY_SIZE];    //16B   
    UINT8 name[NAME_LEN]; //inode 커지면 바꿀게 생긴다.16B
    UINT32 no;              //4B
    UINT32 size;            //4B
    UINT32 version;
    FILE_FTL_META file_ftl_meta;    //28B
    //UINT8 vpn;  //1B

    UINT8 padd[DRAM_ECC_UNIT-(KEY_SIZE+NAME_LEN+4+4+4+4*(NUM_DIRECT_MAP+NUM_INDIRECT_MAP+NUM_DOUBLE_INDIRECT_MAP))];  //현재 128-60bytes

}INODE;

typedef struct HASH_ELE{
    struct HASH_ELE* next; //얘는 무조건 physical address이다.
  //  INODE* inode;
    UINT32 ino;
    char name[NAME_LEN];
}HASH_ELE;

//static UINT8 ds_inode_bmp[DS_INODE_BMP_SIZE]; //1024->128
//bitmap for FTL
//1bit per 512bytes
//static UINT8 ds_ftl_bmp[DS_FTL_BMP_SIZE]; //2048 -> 256

typedef struct SUPER_BLOCK{
    HASH_ELE* name_hash[BUCKET_SIZE];   //table자체는 SRAM, element는 DRAM
    INODE* file_table[DS_MAX_FILE];        
    UINT8 ds_inode_bmp[DS_INODE_BMP_SIZE];  //sram
    UINT8 ds_ftl_bmp[DS_FTL_BMP_SIZE]; 
    UINT8 ds_nhash_bmp[DS_NHASH_BMP_SIZE];
    UINT32 num_inode;
    UINT32 cur_fid;
    UINT8 device_key[KEY_SIZE]; //device key!
}SUPER_BLOCK;

 static SUPER_BLOCK ds_superblock;





//file linked list. each file id is 1~65535. 
//each file is 2+4+16 = 22bytes -> 24bytes
/*
typedef struct FILE_META{
    UINT16 file_id;
    struct FILE_META* next;    //originally, to free file, we have to make bidirectional linked list, but has size problem..
    FILE_FTL_META file_ftl_meta;  
}FILE_META;
*/

//size : KEY_SIZE + sizeof(file_ftl_meta)= 16+16 = 32bytes


//Enclave Key table element.
/*
typedef struct ENCLAVE{
  //  UINT32 public_key;
    UINT8 sym_key[KEY_SIZE];
    FILE_META* file; //void은임시. 
}ENCLAVE;
*/
/*
//enclave key table. index is enclave ID(1~255). each element has public key and file FTL pointer
typedef struct ENC_KEY_TABLE{
    ENCLAVE enc_key_table[ENC_SIZE];
}ENC_KEY_TABLE;
*/

void DS_init_superblock();
UINT32 DS_file_create(const char name[NAME_LEN], const UINT8 key[KEY_SIZE]);
UINT32 DS_file_open(const char name[NAME_LEN]);
void DS_file_remove(UINT32 fid);
UINT8 DS_file_close(UINT32 fid);
UINT32 DS_get_filesize(const UINT32 fid);
UINT32 DS_get_filekey(const UINT32 fid);
void DS_get_devicekey(UINT8* key);
UINT32 DS_get_version_increased(const UINT32 fid);
UINT32 DS_get_version(const UINT32 fid);

UINT32 DS_get_vpn(const SGX_LBA sgx_lba);
void DS_set_vpn(const SGX_LBA sgx_lba, const UINT32 vpn);
void DS_test();
void DS_print_all();
UINT32 DS_get_FTLchunk_addr(INODE* inode, int ftl_chunk);
void DS_set_FTLChunk(INODE *inode, FILE_FTL* ftl_chunk, int chunk_idx);

void DS_store_inode(INODE* inode);
void DS_load_inode(INODE* inode, UINT32 ino);
UINT32 DS_PA_to_VA(const UINT32 paddr);
UINT32 DS_VA_to_PA(const UINT32 vaddr);
void DS_inode_set_size(UINT32 fid, UINT32 offset, UINT32 size);
void DS_inode_set_FTL_NULL(INODE *inode);
//initiate ftl_sgx
/*
void sgx_init();
void sgx_enclave_get_sym_key(UINT8 enclave_id, UINT8 sym_key[KEY_SIZE]);
void sgx_enclave_insert(UINT8 enclave_id, UINT8 sym_key[KEY_SIZE]);
void sgx_file_open(UINT8 enclave_id, UINT16 file_id, UINT32 encrypted_hash, UINT8 sym_key[KEY_SIZE]);

char sgx_enclave_exists(UINT8 enclave_id);
//FILE_META* sgx_file_search(UINT8 enclave_id, UINT16 file_id);
//UINT32 sgx_get_vpn_(FILE_META* file, UINT32 lpn);
//void sgx_set_vpn_(FILE_META *file, UINT32 lpn, UINT32 vpn);

UINT32 sgx_get_vpn(const SGX_LBA sgx_lba);
void sgx_set_vpn(const SGX_LBA sgx_lba, const UINT32 vpn);

void sgx_file_insert(UINT8 enclave_id, UINT16 file_id);
void sgx_test();
void sgx_print_all();
void sgx_tc_write_seq(const SGX_LBA sgx_lba, const UINT32 io_num, const UINT32 sector_size);
*/