#include "ftl_sgx.h"

#include "sha256_sgx.h"
#include "jasmine.h"
//#define FLAG_FILE_FTL 1
//#define FLAG_FILE_INDIRECTED 2
//#define FLAG_FILE_DOUBLED 3

static void DS_init_sb_file_table_space();

static void DS_init_sb_file_table_space();
static void DS_init_sb_name_hash();
static void DS_init_sb_bmp();
static void DS_init_sb_others();
static void DS_init_sb_nhash();

static void DS_init_inode(INODE *inode, const char name[NAME_LEN]);
static int DS_get_inode_no();

static int DS_get_new_idx(const char type);
static void* DS_malloc(const char type);
static void DS_free(void* data, const char type);


static void DS_nhash_insert(const char name[NAME_LEN], const UINT32 ino, INODE* inode);
static void DS_nhash_remove(const char name[NAME_LEN]);
static UINT32 DS_nhash_get_ino(const char name[NAME_LEN]);
static int nhash_func(const char name[NAME_LEN]);

static void DS_inode_remove();
static void DS_inode_search();

static UINT32 DS_filetb_insert(INODE* inode);
static INODE* DS_filetb_remove(const UINT32 fid);
static INODE* DS_filetb_get_inode(const UINT32 fid);

static void DS_set_vpn_(INODE *inode, const UINT32 lpn, const UINT32 vpn);
static UINT32 DS_get_vpn_(const INODE *inode, const UINT32 lpn);

static void DS_inode_free(INODE* inode);
static void DS_FTLmeta_free(INODE* inode);
static void DS_FTL_free(INODE* inode, int chunk_idx);


/************** Here is to initiate the data structures **************************/
//
// Initiate The related data structures : file_table, INODE, FILE_FTL
//
//
//////////////////////////////////////////////////////////////////////////
//this function should be called at first time ssd formated

void DS_init_superblock()
{
    DS_init_sb_file_table_space();
    DS_init_sb_name_hash();
    DS_init_sb_bmp();
    DS_init_sb_others();

}

static void DS_init_sb_file_table_space()
{
    int i;
    for(i=0; i<DS_MAX_FILE; i++)
    {
        ds_superblock.file_table[i]=NULL;
    }
}

static void DS_init_sb_name_hash()
{
    int i;
    for(i=0; i<BUCKET_SIZE; i++)
    {
        ds_superblock.name_hash[i]=NULL;
    }
}
static void DS_init_sb_bmp()
{
    int i;
    for(i=0; i<DS_INODE_BMP_SIZE; i++)
    {
        ds_superblock.ds_inode_bmp[i]=0;
    }
    for(i=0; i<DS_FTL_BMP_SIZE; i++)
    {
        ds_superblock.ds_ftl_bmp[i]=0;
    }
    for(i=0; i<DS_NHASH_BMP_SIZE; i++)
    {
        ds_superblock.ds_nhash_bmp[i]=0;
    }
}

static void DS_init_sb_others()
{
    ds_superblock.num_inode=1;
    ds_superblock.cur_fid=1;
}

static void DS_init_sb_nhash()
{
    //1. load namehash
    /* not implemented */
    
}

static void DS_init_inode(INODE *inode, const char name[NAME_LEN])
{
    int i;
    FILE_FTL_META file_ftl_meta;

    inode->no = DS_get_inode_no();
    inode->size = 0;
    inode->version = 1; //random이 낫긴 함...
    for(i=0; i<NAME_LEN; i++)
        inode->name[i] = 0;
    memcpy(inode->name, name, NAME_LEN);
    _mem_set_sram((UINT32)inode->key, 0x00, KEY_SIZE);
    
    //이 밑도 수정해야함.
    for(i=0; i<NUM_DIRECT_MAP; i++) 
        file_ftl_meta.direct_mapped[i]=NULL;
    for(i=0; i<NUM_INDIRECT_MAP; i++) 
        file_ftl_meta.indirect_mapped[i]=NULL;
    for(i=0; i<NUM_DOUBLE_INDIRECT_MAP; i++) 
        file_ftl_meta.double_indirect_mapped[i]=NULL;

   //DS_inode_set_FTL_meta(inode, file_ftl_meta);
    inode->file_ftl_meta = file_ftl_meta;
}

static int DS_get_inode_no()
{
    return ds_superblock.num_inode++;
}

/************** Here is to malloc / free  **************************/
//
//  allocate / free INODE, file FTL
//  DS_malloc(type) : allocate new data structure (type : INODE, FTL)
//  DS_free(data, type) : free data structure
//////////////////////////////////////////////////////////////////////////

//maybe test is needed.
static int DS_get_new_idx(const char type)
{
    int i,j,jmp;
    UINT8 tmp;
    int bmp_size;
    UINT8 *bmp;
    char ERR=0;

    if(type==TYPE_INODE)
    {
        bmp_size = DS_INODE_BMP_SIZE;
        bmp = ds_superblock.ds_inode_bmp;
    }
    else if(type==TYPE_FTL)
    {
        bmp_size = DS_FTL_BMP_SIZE;
        bmp = ds_superblock.ds_ftl_bmp;
    }
    else if(type==TYPE_NHASH)
    {
        bmp_size = DS_NHASH_BMP_SIZE;
        bmp = ds_superblock.ds_nhash_bmp;
    }
    else{
        ERR=1;
        ;//uart_printf("ERR[DS_get_new_idx] : type error");
    }

    jmp=0;
    for(i=0; i<bmp_size; i++)
    {
        if(bmp[i] != 0xff)
        {
            tmp=bmp[i];
            for(j=0; j<8; j++)  //1byte는 8bit
            {
                if((tmp & 0x80) == 0 )   //MSB 비교. 0이면 break
                {   
                    jmp=1;
                    break;
                }
                else
                {
                    tmp = tmp<<1;
                }
           }
            if(jmp==1)
            {
                //[i, j] is the index of new inode
                //convert the ds_inode_bmp (as full)
                bmp[i] = bmp[i] | ( 0x01 << (7-j) );
               // uart_printf("[DS_get_new_idx} : return : %d\n", i*8+j);
                //uart_printf("[DS_get_new_idx] : %d %d %d", i,j,i*8+j);
                return i*8+j;
            }
        }
    }
   // uart_printf("ERR[DS_get_nex_idx]");
    //uart_printf("[DS_get_new_idx] Erorr: memory full!");
}

static void DS_setNULL(UINT32 paddr, UINT32 size)
{
    int i;
    for(i=0; i<size; i++)
    {
        *((UINT8*)(paddr+i))=0;
    }
}

//INODE, NHASH는 malloc시 physical address를 리턴한다.
//따라서 포인터를 바로 사용할 수 있다.
static void* DS_malloc(const char type)
{
    UINT32 vaddr, paddr;
    char ERR=0;
   // uart_printf("[DS_malloc]\n");
    if(type==TYPE_INODE)
    {
        vaddr = DS_INODE_ADDR + DS_get_new_idx(type) * DS_INODE_SIZE;
        paddr = DS_VA_to_PA(vaddr);
       
        DS_setNULL(paddr, DS_INODE_SIZE);
        
        if(vaddr > DS_INODE_ADDR+DS_INODE_BYTES)
        {
            ERR=1;
            //uart_printf("ERR[DS_malloc] : TYPE_INODE: too large address!");
        }
               // uart_printf("[DS_malloc] vinode, pinode : %x %x", vaddr, paddr);
        
    }
    else if(type==TYPE_FTL) 
    {
        vaddr = DS_FTL_ADDR + DS_get_new_idx(type) * DS_FTL_SIZE;
        paddr = DS_VA_to_PA(vaddr);
        DS_setNULL(paddr, DS_FTL_SIZE);

        if(vaddr > DS_FTL_ADDR+DS_FTL_BYTES)
        {
            ERR=1;
            //uart_printf("ERR[DS_malloc] : TYPE_FTL: too large address!");
        }
       // _mem_set_dram(vaddr, 0x00, DS_FTL_SIZE);

        //return (void*)DS_VA_to_PA(vaddr);
    }
    else if(type==TYPE_NHASH)
    {
        vaddr = DS_NHASH_ADDR + DS_get_new_idx(type) * DS_NHASH_SIZE;
        paddr = DS_VA_to_PA(vaddr);
        DS_setNULL(paddr, DS_NHASH_SIZE);

         if(vaddr > DS_NHASH_ADDR+DS_NHASH_BYTES)
        {
            ERR=1;
            //uart_printf("ERR[DS_malloc] : TYPE_NHASH: too large address!");
        }

         //_mem_set_dram(vaddr, 0x00, DS_NHASH_SIZE);
        // return (void*)DS_VA_to_PA(DS_FTL_ADDR + DS_get_new_idx(type) * DS_NHASH_SIZE);
    }
    else{
        ERR=1;
        //uart_printf("ERR[DS_malloc] : type error");
    }
    if(ERR==1)   {
     //   uart_printf("ERR[DS_malloc]");
     ;
    }
    return (void*)paddr;
}

//INODE, NHASH는 free시 VA로 변환해줘야한다.
static void DS_free(void* data, const char type)
{
    int free_idx;
    int i,j;
    UINT8 *bmp;

    data = (void*)DS_PA_to_VA((UINT32)data); //지울때는 virtual address로 변환해야함.

    if(type == TYPE_INODE)
    {
        //_mem_set_dram((UINT32)data, 0x00, DS_INODE_SIZE);   //NULL로 만들어줘야함.
        free_idx = ((UINT32) data - DS_INODE_ADDR) / DS_INODE_SIZE;
        bmp = ds_superblock.ds_inode_bmp;
    }
    else if(type==TYPE_FTL)
    {
        //_mem_set_dram((UINT32)data, 0x00, DS_FTL_SIZE);
        free_idx = ((UINT32) data - DS_FTL_ADDR) / DS_FTL_SIZE;
        bmp = ds_superblock.ds_ftl_bmp;
    }
    else if(type==TYPE_NHASH)
    { 
        // _mem_set_dram((UINT32)data, 0x00, DS_NHASH_SIZE);
        free_idx = ((UINT32) data - DS_NHASH_ADDR) / DS_NHASH_SIZE;
        bmp = ds_superblock.ds_nhash_bmp;
    }

    i=free_idx/8;
    j=free_idx%8;

    bmp[i] = bmp[i] & (~(0x01 << (7-j)));
}

UINT32 DS_VA_to_PA(const UINT32 vaddr)
{
    UINT32 paddr;
	ASSERT(vaddr >= DRAM_BASE && vaddr < (DRAM_BASE + DRAM_SIZE));
	
	paddr = (DRAM_BASE + (vaddr - DRAM_BASE)/128*132 + (vaddr - DRAM_BASE)%128);

	return paddr;
}

UINT32 DS_PA_to_VA(const UINT32 paddr)
{
    UINT32 vaddr;
    
    //vaddr = (DRAM_BASE + (paddr-DRAM_BASE) + (paddr-DRAM_BASE)%128);
    vaddr = paddr - (paddr - DRAM_BASE)/132*4;  //132bytes당 4bytes씩 줄여야하므로.
    return vaddr;
}

/************** Here is data structure function **************************/
// name_hash : insert, remove, search
// inode : insert, remove, search, set_inode_key, set_inode_no, set_vpn, get_inode_key, get_inode_no, get_vpn
// FTL : ??
//////////////////////////////////////////////////////////////////////////

/*************name_hash*****************/
//name_hash : insert, remove, search
//return : HASH_ELE

static void DS_nhash_insert(const char name[NAME_LEN], const UINT32 ino, INODE* inode)
{
    HASH_ELE *cur, *new, *next;
    char ERR=0;
    int i;
    int idx = nhash_func(name);
   // UINT32 table_addr = (DS_VA_to_PA(&ds_superblock.name_hash[nhash_func(name)]));
    //HASH_ELE *cur = *((HASH_ELE)table_addr);
    //새로 file create시 inode 새로 만들고 리턴.
    //HASH_ELE *cur = (HASH_ELE*)(*(DS_VA_to_PA(&ds_superblock.name_hash[nhash_func(name)])));
    cur = ds_superblock.name_hash[idx];

    new = (HASH_ELE*) DS_malloc (TYPE_NHASH);
    new->next = NULL;
    new->ino = ino;
    //n/ew->inode = inode;
    for(i=0; i<NAME_LEN; i++)
        new->name[i] = 0;
    memcpy(new->name, name, strlen(name));
    
    if(cur==NULL)   //no collision
    {
        //uart_printf("insert idx %d %x", idx, inode);
        ds_superblock.name_hash[idx]=new;
    }

    else
    {
        next=cur->next;
        while (next!=NULL)   //collision
        {
            if(cur->ino == ino)
           // if(memcmp(cur->inode->name, name) == 0) 
            {
                ;//
                //uart_printf("ERR[DS_nhash_insert]");
                //uart_printf("Error [DS_nhash_insert] : Duplicated Name\n");
                //return NULL;
                return;
            }
            cur = cur->next;
            next = cur->next;
        }
        cur->next = new;
    }
    return;
    //return new;
}

static void DS_nhash_remove(const char name[NAME_LEN])
{
    HASH_ELE *cur, *prev;
    int idx = nhash_func(name);
    cur = ds_superblock.name_hash[idx];
    prev = cur;

    if(memcmp(cur->name, name, strlen(name))==0)
    {
        //free(cur);
        DS_free(cur, TYPE_NHASH);
        ds_superblock.name_hash[idx]=NULL;
        return;
    }

    else
    {
        while(cur!=NULL)
        {
            prev=cur;
            cur = cur->next;
            
            if(memcmp(cur->name, name, strlen(name))==0)
            {
                 //remove this inode!
                prev->next = cur->next;
                //free(cur);
                DS_free(cur, TYPE_NHASH);
                return;
            }
        }
    }

   ;// uart_printf("Err [DS_nhash_remvoe] no such ino\n");
}

static UINT32 DS_nhash_get_ino(const char name[NAME_LEN])
{
    //uart_printf("[DS_nhash_get_ino]");
    int idx = nhash_func(name);
    int d = strlen(name);
  //  uart_printf("idx : %d %d", idx, strlen(name));
    HASH_ELE *cur = ds_superblock.name_hash[idx];
   // uart_printf("%s", name);

    while(cur!=NULL)
    {
       // uart_printf("z");
      //  uart_printf("name : %s", name);
       
        //uart_printf("inode name : %s %s %d", name, cur->inode->name);
        //uart_printf("z %s %x", cur->inode->name, cur->inode);
        if(memcmp(cur->name, name, strlen(name)) == 0)
        {
         //   uart_printf("I found! let's return it : %d", cur->ino);
            return cur->ino;
        }
        cur = cur->next;
    }

  //  uart_printf("can't find inode!");
    return -1;    //name이 없으면-1리턴
}


static int nhash_func(const char name[NAME_LEN])
{
    //hash function
    int i=0;
    int idx=0;
    while(name[i]!='\0' )
    {
        if(i>=NAME_LEN)
        {
            ;
            //uart_printf("ERR[nhash_func]");
             //uart_printf("ERROR : Too long name path\n");
        }

        idx += name[i];
        i++;
    }
    idx = idx % BUCKET_SIZE;
    return idx;
}


/*************INODE****************/
//inode : allocate, remove, search, set_inode_key, set_inode_no, set_vpn, get_inode_key, get_inode_no, get_vpn
// get key, get name, get no, get ftl
// set key, set name, set no, set FTL meta
//

static void DS_inode_remove()
{
    /* EMPTY */
}

static void DS_inode_search()
{
 //
}

void DS_inode_set_size(UINT32 fid, UINT32 offset, UINT32 size)
{
    INODE *inode = DS_filetb_get_inode(fid);
    inode->size = (inode->size > offset+size) ? (inode->size) : (offset+size);
    return;
}

void DS_inode_set_FTL_NULL(INODE *inode)
{
    int i;
    for (i=0; i<NUM_DIRECT_MAP; i++)
        inode->file_ftl_meta.direct_mapped[i] = NULL;
    for (i=0; i<NUM_INDIRECT_MAP; i++)
        inode->file_ftl_meta.indirect_mapped[i] = NULL;
    for (i=0; i<NUM_DOUBLE_INDIRECT_MAP; i++)
        inode->file_ftl_meta.double_indirect_mapped[i] = NULL;
}

static void DS_inode_free(INODE* inode)
{
    //ftl metadata 모두 free해줘야함.
    DS_FTLmeta_free(inode);
    DS_free(inode, TYPE_INODE);
}
static void DS_FTLmeta_free(INODE* inode)
{
    UINT32 DS_filesize_in_FTLchunk = BYTES_PER_PAGE * (DS_FTL_SIZE/4); //32KB*32 = 1MB
    //우리는 FTL의 청크 단위로 복사를할것임.
    UINT32 num_FTLchunk = (inode->size + DS_filesize_in_FTLchunk -1) / (DS_filesize_in_FTLchunk);

    int chunk_idx;
    for(chunk_idx=0; chunk_idx<num_FTLchunk; chunk_idx++)
    {
        DS_FTL_free(inode, chunk_idx);
    }
}

static void DS_FTL_free(INODE* inode, int chunk_idx)
{
    int i,j, k;
    int temp;

    if(chunk_idx < NUM_DIRECT_MAP)  //0,1
    {
        i=chunk_idx;
        DS_free(inode->file_ftl_meta.direct_mapped[i], TYPE_FTL);
    }
    else if(chunk_idx < FILE_FTL_NUM * NUM_INDIRECT_MAP + NUM_DIRECT_MAP)
    {
        temp = chunk_idx - NUM_DIRECT_MAP;
        i = temp / (FILE_FTL_NUM);
        j = temp % (FILE_FTL_NUM);

        DS_free(inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j], TYPE_FTL);

        if(j==FILE_FTL_NUM-1)
        {
            DS_free(inode->file_ftl_meta.indirect_mapped[i], TYPE_FTL);
        }
    }
    else if (chunk_idx < FILE_FTL_NUM * FILE_FTL_NUM * NUM_DOUBLE_INDIRECT_MAP\
     +  FILE_FTL_NUM * NUM_INDIRECT_MAP +  NUM_DIRECT_MAP)
    {
        temp = chunk_idx - (FILE_FTL_NUM * NUM_INDIRECT_MAP + NUM_DIRECT_MAP);
        i = temp / (FILE_FTL_NUM * FILE_FTL_NUM);
        temp = temp % (FILE_FTL_NUM * FILE_FTL_NUM);
        j = temp / (FILE_FTL_NUM);
        k = temp % (FILE_FTL_NUM); 

        DS_free(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k], TYPE_FTL);

        if(k==FILE_FTL_NUM-1)
        {
            DS_free(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j], TYPE_FTL);
        }
        if(j==FILE_FTL_NUM-1)
        {
            DS_free(inode->file_ftl_meta.double_indirect_mapped[i], TYPE_FTL);
        }
    }
}


/*
static UINT8* DS_inode_get_key(INODE* inode)
{
    INODE *physical_inode = (INODE*)DS_VA_to_PA(inode);
    return physical_inode->key;
}
static UINT8* DS_inode_get_name(INODE *inode)
{
    INODE *physical_inode = (INODE*)DS_VA_to_PA(inode);
    return physical_inode->name;
}
static UINT32 DS_inode_get_no(INODE *inode)
{
    INODE *physical_inode = (INODE*)DS_VA_to_PA(inode);
    return physical_inode->no;
}

static FILE_FTL_META DS_inode_get_FTL_meta(INODE *inode)
{
    INODE *physical_inode = (INODE*)DS_VA_to_PA(inode);
    return physical_inode->file_ftl_meta;
}


static void DS_inode_set_key(INODE* inode, UINT8 key[KEY_SIZE])
{
    int i;
    INODE * physical_inode = (INODE*)DS_VA_to_PA(inode);
    for(i=0; i<KEY_SIZE; i++)
        physical_inode->key[i]=key[i];
}

static void DS_inode_set_name(INODE* inode, char name[NAME_LEN])
{
    int i;
    INODE * physical_inode = (INODE*)DS_VA_to_PA(inode);
    for (i=0; i<NAME_LEN; i++)
        physical_inode->name[i] = name[i];
}

static void DS_inode_set_no(INODE* inode, UINT32 no)
{
    INODE * physical_inode = (INODE*)DS_VA_to_PA(inode);
    physical_inode->no = no;
}

static void DS_inode_set_FTL_meta(INODE* inode, FILE_FTL_META file_ftl_meta)
{
     INODE * physical_inode = (INODE*)DS_VA_to_PA(inode);
     physical_inode->file_ftl_meta = file_ftl_meta;
}
*/



/*************file table****************/
// insert , remove, search
// 
//
//
//

static UINT32 DS_filetb_insert(INODE* inode)
{
    UINT32 next_fid = ds_superblock.cur_fid;
   // uart_printf("[DS_filetb_insert] %d %d", ds_superblock.cur_fid, next_fid);
   // UINT32 fid=ds_superblock.num_fid++;
   //해당 노드가 비어있는지 체크. 빌떄까지 뻉뻉이.
    int i=0;
    while(ds_superblock.file_table[next_fid] != NULL)
    {
       // uart_printf("?");
        next_fid = (next_fid + 1) % DS_MAX_FILE;
        i++;
        if(i>DS_MAX_FILE)
        {
          //  uart_printf("ERR[DS_filetb_insert]");
            ;//uart_printf("ERR[DS_filetb_insert] : file table full!");
        }

    }
    ds_superblock.file_table[next_fid] = inode;
    ds_superblock.cur_fid=next_fid;
    //uart_printf("next fid : %d", next_fid);
    return next_fid;
}

static INODE* DS_filetb_remove(const UINT32 fid)
{
    /* not yet*/
    INODE* inode = ds_superblock.file_table[fid];
    ds_superblock.file_table[fid]=NULL;
    return inode;
}

static INODE* DS_filetb_get_inode(const UINT32 fid)
{
    return ds_superblock.file_table[fid];
}


/************** Top level function **************************/
//  file create , file open, file close, file remove
//
//
//////////////////////////////////////////////////////////////////////////

UINT32 DS_file_create(const char name[NAME_LEN], const UINT8 key[KEY_SIZE])
{
    //HASH_ELE *new_hash_ele;
    UINT32 fid;
    INODE *inode;
    int i;
    //INODE *new_inode;

    // 1. verification with device key
    /*EMPTY*/

    //2. Allocate new inode
    inode = (INODE*) DS_malloc(TYPE_INODE);
    DS_init_inode(inode, name);

    //key 등록
    for(i=0; i<KEY_SIZE; i++)
        inode->key[i] = key[i];

    //3. insert in name hash
    DS_nhash_insert(name, inode->no, inode);

    //4. insert file table
    fid = DS_filetb_insert(inode);

    return fid;
/*
    // /////2. Insert name hash
   // new_hash_ele = DS_nhash_insert(name, NULL);
    if(new_hash_ele)
    {
        // 3. Allocate new inode
        new_hash_ele -> inode = (INODE*) DS_malloc(TYPE_INODE);
        DS_init_inode(new_hash_ele -> inode, name);

        // 4. insert file table
        fid = DS_filetb_insert(new_hash_ele -> inode);
    }
    else
    {
        uart_printf("ERR [DS_file_create] \n");
    }
    return fid;
    */
}

void DS_file_remove(UINT32 fid)
{
    ;
}
//load와 관련된 모든 구현은 아직 not yet.
UINT32 DS_file_open(const char name[NAME_LEN])
{
    UINT32 fid;
    //UINT32 size;
    //1. search nmae hashtable
    //uart_printf("[DS_file_open]");
    UINT32 ino = DS_nhash_get_ino(name);
    //uart_printf("open ino : %d", ino);
    if(ino==-1) //name이 없음. 생성된 적 없는파일
        return FILE_NOT_EXISTS;
    //uart_printf("[DS_file_open] %s %d", name, ino);
    UINT8 *key;
    //2. load inode with ino.
    /* Not implemented*/
    INODE* inode;
    //2-1. Allocate new inode
    inode = (INODE*) DS_malloc(TYPE_INODE);
    //DS_init_inode(inode, name);
    
    //2-2. load inode from device


    DS_load_inode(inode, ino);
    //inode = DS_load_inode(ino);

    //3. get key
    //key = inode->key;
   // key=DS_inode_get_key(inode);

    //4. verification

    //5. Insert file table
    fid =  DS_filetb_insert(inode);

    //*size = inode->size;
    return fid;

}

UINT8 DS_file_close(UINT32 fid)
{
    int i;
    //1. remove element from file table
    //uart_printf("[DS_file_close]");

   // uart_print("3");

    //before close, you have to temporarily store the key to find it on RAF flow
    for(i=0; i<KEY_SIZE; i++)
        key_tp[i] = ((UINT8*)DS_get_filekey(fid))[i];

    INODE* inode = DS_filetb_remove(fid);
    //uart_printf("inode no : %d", inode->no);
// uart_printf("3 %d", inode->no);
    //2. store inode
    DS_store_inode(inode); 
   //     uart_print("3");
    //3. free inode
    DS_inode_free(inode);
    // uart_print("3");
    //DS_free(inode, TYPE_INODE);
    //uart_printf("[DS_file_close] closed");
    return 1;
}

UINT32 DS_get_filesize(const UINT32 fid)
{
    return ds_superblock.file_table[fid]->size;
}
UINT32 DS_get_filekey(const UINT32 fid)
{
    return ds_superblock.file_table[fid]->key;
}
UINT32 DS_get_tempkey_closerd()
{
    return key_tp;
}
void DS_remove_tempkey_closerd()
{
    int i;
    for(i=0; i<KEY_SIZE; i++)   key_tp[i] = 0;
    return;
}

void DS_get_devicekey(UINT8* key)
{
    key = ds_superblock.device_key;
}
//오직 rdafwr만 얘를 호출한다.
UINT32 DS_get_version_increased(const UINT32 fid)
{
    ds_superblock.file_table[fid]->version++;  //version 증가 
    return ds_superblock.file_table[fid]->version;
}

UINT32 DS_get_version(const UINT32 fid)
{
   // ds_superblock.file_table[fid]->version++;  //version 증가 
    return ds_superblock.file_table[fid]->version;
}


UINT32 DS_get_FTLchunk_addr(INODE* inode, int ftl_chunk)
{
    //UINT32 ftl_chunk_addr;
    
   // UINT32 vpn;
   /// UINT32 t_lpn;
    int temp;
    int i,j,k;
    int tt=0;
    char ERR=0;
    
   // uart_printf("[DS_get_vpn_]\n");
   // uart_printf("lpn : %x\n", lpn);
    //direct map
   // uart_printf("sgx_get_vpn_....\n");
   // lpn = ftl_chunk*32;

    if(ftl_chunk < NUM_DIRECT_MAP)  //0,1
    {
        //uart_printf("[DS_get_FTLChunk] : %d, %x", ftl_chunk, inode->file_ftl_meta.direct_mapped[i]->vpn[2]);
        
        tt=1;
        i=ftl_chunk;
        //uart_printf("[DS_get_FTLChunk] : %d, %x", ftl_chunk, inode->file_ftl_meta.direct_mapped[i]->vpn[2]);
        
        /*
        for(i=0; i<FILE_FTL_NUM; i++)
        {
            uart_printf(" %x", inode->file_ftl_meta.direct_mapped[i]->vpn[i]);
        }
        */
        if(inode->file_ftl_meta.direct_mapped[i])
        {
         //   uart_printf("getftl %x", inode->file_ftl_meta.direct_mapped[i]->vpn[0]);
            return (UINT32)inode->file_ftl_meta.direct_mapped[i];
        }
        else
            ERR=1;
            //uart_printf("1ERR[DS_get_FTLchunk_addr] : ftl_chunk doesn't exists");
    }

    //indirect map
    else if(ftl_chunk < FILE_FTL_NUM * NUM_INDIRECT_MAP + NUM_DIRECT_MAP)
    {
        tt=2;
        temp = ftl_chunk - NUM_DIRECT_MAP;
        i = temp / (FILE_FTL_NUM);
        j = temp % (FILE_FTL_NUM);
        if(inode->file_ftl_meta.indirect_mapped[i] \
            && inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j])
        {
            return (UINT32)inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j];
        }
          else
            ERR=1;;
            //uart_printf("2ERR[DS_get_FTLchunk_addr] : ftl_chunk doesn't exists");

    }
    //double indirect map
    else if (ftl_chunk < FILE_FTL_NUM * FILE_FTL_NUM * NUM_DOUBLE_INDIRECT_MAP\
     +  FILE_FTL_NUM * NUM_INDIRECT_MAP +  NUM_DIRECT_MAP)
    {
        tt=3;
        temp = ftl_chunk - (FILE_FTL_NUM * NUM_INDIRECT_MAP + NUM_DIRECT_MAP);
        i = temp / (FILE_FTL_NUM * FILE_FTL_NUM);
        temp = temp % (FILE_FTL_NUM * FILE_FTL_NUM);
        j = temp / (FILE_FTL_NUM);
        k = temp % (FILE_FTL_NUM); 

        if(inode->file_ftl_meta.double_indirect_mapped[i] \
           && inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j] \
           && inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k])
        {
            return (UINT32)inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k];
        }
          else
            ERR=1;;
           // uart_printf("ERR[DS_get_FTLchunk_addr] : ftl_chunk doesn't exist");
    
    }
    if(ERR==1)
    {
        ;
        //uart_printf("ERR[DS_get_FTLchunk_addr]");
    }
  //  uart_printf("//\n");
    //uart_printf("[get_vpn_] lpn : %x vpn : %x\n",lpn, vpn);
    //uart_printf("[DS_get_FTLchunk_addr] error : %d no address here.", tt);
    return (UINT32)NULL;   //Error
}

void DS_set_FTLChunk(INODE *inode, FILE_FTL* ftl_chunk, int chunk_idx)
{
    int temp;
    int i,j,k;
    
    if(chunk_idx < NUM_DIRECT_MAP)  //0,1
    {
        i=chunk_idx;
       //if(inode->file_ftl_meta.direct_mapped[i])
         //   return inode->file_ftl_meta.direct_mapped[i];
        if(inode->file_ftl_meta.direct_mapped[i]==NULL) 
        {
            inode->file_ftl_meta.direct_mapped[i] = (FILE_FTL*)DS_malloc(TYPE_FTL);
            //uart_printf("zzz");
            //mem_copy(inode->file_ftl_meta.direct_mapped[i], ftl_chunk, DS_FTL_SIZE);
        }
        //uart_printf("[DS_set_FTLChunk] 1");
       // uart_printf("bf[DS_set_FTLChunk] : %d : %x %x", i,inode->file_ftl_meta.direct_mapped[i]->vpn[2], ftl_chunk->vpn[2]);
        mem_copy(DS_PA_to_VA(inode->file_ftl_meta.direct_mapped[i]), ftl_chunk, DS_FTL_SIZE);
        //uart_printf("setftl %x", inode->file_ftl_meta.direct_mapped[i]->vpn[0]);

        //uart_printf("af[DS_set_FTLChunk] 1: %d : %x %x", i,inode->file_ftl_meta.direct_mapped[i]->vpn[2], ftl_chunk->vpn[2]);
        //mem_copy(inode->file_ftl_meta.direct_mapped[i], ftl_chunk, DS_FTL_SIZE);
        //uart_printf("af[DS_set_FTLChunk] 2: %d : %x %x", i,inode->file_ftl_meta.direct_mapped[i]->vpn[2], ftl_chunk->vpn[2]);
        
         /*
          for(i=0; i<FILE_FTL_NUM; i++)
        {
            uart_printf("%x", inode->file_ftl_meta.direct_mapped[i]->vpn[i]);
        }
        */
        //inode->file_ftl_meta.direct_mapped[i]->vpn[j] = vpn;
       // mem_copy(inode->ft)
    }
    //indirect map
    else if(chunk_idx < FILE_FTL_NUM * NUM_INDIRECT_MAP + NUM_DIRECT_MAP)
    {
        temp = chunk_idx - NUM_DIRECT_MAP;
        i = temp / (FILE_FTL_NUM);
        j = temp % (FILE_FTL_NUM);

        if(inode->file_ftl_meta.indirect_mapped[i]==NULL)
            inode->file_ftl_meta.indirect_mapped[i] = (FILE_INDIRECTED*)DS_malloc(TYPE_FTL);
        if(inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]==NULL)
           inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j] = (FILE_FTL*)DS_malloc(TYPE_FTL);
        //uart_printf("[DS_set_FTLChunk] 2");
        mem_copy(DS_PA_to_VA(inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]), ftl_chunk, DS_FTL_SIZE);
       // inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]->vpn[k] = vpn;
        
    }
     else if (chunk_idx < FILE_FTL_NUM * FILE_FTL_NUM * NUM_DOUBLE_INDIRECT_MAP\
     +  FILE_FTL_NUM * NUM_INDIRECT_MAP +  NUM_DIRECT_MAP)
    {
        temp = chunk_idx - (FILE_FTL_NUM * NUM_INDIRECT_MAP + NUM_DIRECT_MAP);
        i = temp / (FILE_FTL_NUM * FILE_FTL_NUM);
        temp = temp % (FILE_FTL_NUM * FILE_FTL_NUM);
        j = temp / (FILE_FTL_NUM);
        k = temp % (FILE_FTL_NUM); 

        if(inode->file_ftl_meta.double_indirect_mapped[i]==NULL)
            inode->file_ftl_meta.double_indirect_mapped[i] = (FILE_DOUBLED*)DS_malloc(TYPE_FTL);
        if(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]==NULL)
            inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j] = (FILE_INDIRECTED*)DS_malloc(TYPE_FTL);
        if(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k] == NULL)
            inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k] = (FILE_FTL*)DS_malloc(TYPE_FTL);
        //uart_printf("[DS_set_FTLChunk] 3");
        mem_copy(DS_PA_to_VA(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k]), ftl_chunk, DS_FTL_SIZE);
       
//        inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k]->vpn[l] = vpn;
    }
    else{
        ;
    //    uart_printf("ERR[DS_set_FTLChunk]");
    }
}
/*
static void DS_load_inode(UINT32 ino)
{

}

static void DS_store_inode(UINT32 ino)
{
    
}
*/

/************** Top level function -FTL**************************/
//  set_vpn, get_vpn
//
//
//////////////////////////////////////////////////////////////////////////

UINT32 DS_get_vpn(const SGX_LBA sgx_lba)
{
    INODE *inode = DS_filetb_get_inode(sgx_lba.fid);
    if(inode)
    {

       // uart_printf("[DS_get_vpn] inode no(%d), name(%s)\n", inode->no, inode->name);
        return DS_get_vpn_(inode, sgx_lba.addr.lpn);
    }
    
 }

void DS_set_vpn(const SGX_LBA sgx_lba, const UINT32 vpn)
{
   // CHECK_LPAGE(lpn);
   // ASSERT(vpn >= (META_BLKS_PER_BANK * PAGES_PER_BLK) && vpn < (VBLKS_PER_BANK * PAGES_PER_BLK));
    //uart_printf("[DS_set_vpn] sgx_lba : %d %d ", sgx_lba.fid, sgx_lba.addr.lpn);
    INODE *inode = DS_filetb_get_inode(sgx_lba.fid);
    if(inode)
    {
        //uart_printf("[DS_set_vpn] inode no(%d), name(%s)\n", inode->no, inode->name);
      //  uart_printf("set_vpn %x %x", sgx_lba.addr.lpn, vpn);
        DS_set_vpn_(inode, sgx_lba.addr.lpn, vpn);
    }
}

static void DS_set_vpn_(INODE *inode, const UINT32 lpn, const UINT32 vpn)
//void sgx_set_vpn_(FILE_META *file_meta, UINT32 lpn, UINT32 vpn)
{
    UINT32 t_lpn;
    int i,j,k,l;
    //FILE_FTL_META *file_ftl = file_meta->file_ftl_meta
    //direct map
 
    if((UINT32)lpn < FILE_FTL_NUM * NUM_DIRECT_MAP)
    {
        i=lpn/FILE_FTL_NUM;
        j=lpn%FILE_FTL_NUM;
       // uart_printf("i,j : %d %d\n", i,j);
        if(inode->file_ftl_meta.direct_mapped[i]==NULL) 
        {
            inode->file_ftl_meta.direct_mapped[i] = (FILE_FTL*)DS_malloc(TYPE_FTL);
           // uart_printf("memory check\n");
            /*
            for(i=0; i<4; i++)
            {
                for(j=0; j<8; j++)
                    uart_printf("%x ", inode->file_ftl_meta.direct_mapped[i]->vpn[i*8+j]);
                uart_printf("\n");
            }
            */

        }
        inode->file_ftl_meta.direct_mapped[i]->vpn[j] = vpn;
        
       // uart_printf("[DS_set_vpn_] %d %d %d, %x", lpn, i, j, inode->file_ftl_meta.direct_mapped[i]->vpn[j]);
        //uart_printf("[DS_set_vpn_] direct : lpn : %d, %d %d" , lpn, i, j);

    }

    //indirect map
    else if((UINT32)lpn < FILE_FTL_NUM * FILE_FTL_NUM * NUM_INDIRECT_MAP + FILE_FTL_NUM * NUM_DIRECT_MAP)
    {
        t_lpn = lpn - FILE_FTL_NUM * NUM_DIRECT_MAP;
        i = t_lpn / (FILE_FTL_NUM * FILE_FTL_NUM);
        t_lpn = t_lpn %  (FILE_FTL_NUM * FILE_FTL_NUM);
        j = t_lpn / FILE_FTL_NUM;
        k = t_lpn % FILE_FTL_NUM;
        
        if(inode->file_ftl_meta.indirect_mapped[i]==NULL)
            inode->file_ftl_meta.indirect_mapped[i] = (FILE_INDIRECTED*)DS_malloc(TYPE_FTL);
        if(inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]==NULL)
           inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j] = (FILE_FTL*)DS_malloc(TYPE_FTL);
        inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]->vpn[k] = vpn;
       // uart_printf("[DS_set_vpn_] indirect : lpn : %d, %d %d %d" , lpn, i, j, k);
    }
    //double indirect map
    else if ((UINT32)lpn < FILE_FTL_NUM * FILE_FTL_NUM * FILE_FTL_NUM * NUM_DOUBLE_INDIRECT_MAP\
     + FILE_FTL_NUM * FILE_FTL_NUM * NUM_INDIRECT_MAP + FILE_FTL_NUM * NUM_DIRECT_MAP)
    {
        t_lpn = lpn - ( FILE_FTL_NUM * FILE_FTL_NUM * NUM_INDIRECT_MAP + FILE_FTL_NUM * NUM_DIRECT_MAP);
        i = t_lpn / (FILE_FTL_NUM * FILE_FTL_NUM * FILE_FTL_NUM);
        t_lpn = t_lpn % (FILE_FTL_NUM * FILE_FTL_NUM * FILE_FTL_NUM);
        j = t_lpn / (FILE_FTL_NUM * FILE_FTL_NUM);
        t_lpn = t_lpn % (FILE_FTL_NUM * FILE_FTL_NUM);
        k = t_lpn / FILE_FTL_NUM;
        l = t_lpn % FILE_FTL_NUM;

        if(inode->file_ftl_meta.double_indirect_mapped[i]==NULL)
            inode->file_ftl_meta.double_indirect_mapped[i] = (FILE_DOUBLED*)DS_malloc(TYPE_FTL);
        if(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]==NULL)
            inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j] = (FILE_INDIRECTED*)DS_malloc(TYPE_FTL);
        if(inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k] == NULL)
            inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k] = (FILE_FTL*)DS_malloc(TYPE_FTL);

        inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k]->vpn[l] = vpn;
        //uart_printf("[DS_set_vpn_] lpn: %d, %d %d %d %d", lpn, i, j, k, l);
    }
    else{
       ;// uart_printf("ERR [DS_set_vpn_]");
    }
     //uart_printf("[set_vpn_] lpn : %x vpn : %x\n",lpn, vpn);
    //uart_printf("//\n");
}

static UINT32 DS_get_vpn_(const INODE *inode, const UINT32 lpn)
//static UINT32 sgx_get_vpn_(FILE_META *file_meta, UINT32 lpn)
{
    UINT32 vpn;
    UINT32 t_lpn;
    
    int i,j,k,l;
   // uart_printf("[DS_get_vpn_]\n");
   // uart_printf("lpn : %x\n", lpn);
    //direct map
   // uart_printf("sgx_get_vpn_....\n");
    if(lpn < FILE_FTL_NUM * NUM_DIRECT_MAP)
    {
        i=lpn/FILE_FTL_NUM;
        j=lpn%FILE_FTL_NUM;
     //   uart_printf("i, j : %d %d\n", i, j);
        if(inode->file_ftl_meta.direct_mapped[i]==NULL \
        || inode->file_ftl_meta.direct_mapped[i]->vpn[j]==NULL)
        {
            //uart_print("1 fail get vpn");
            return (UINT32)NULL;
        }    
       // uart_print("1 success get vpn");
       // uart_printf("%d %d\n", file_meta->file_ftl_meta.direct_mapped[i]\
        , file_meta->file_ftl_meta.direct_mapped[i]->vpn[j]);
       
         vpn = inode->file_ftl_meta.direct_mapped[i]->vpn[j];
         // if(j==2)
          //  uart_printf("[DS_get_vpn_] %d, %x", i, inode->file_ftl_meta.direct_mapped[i]->vpn[2]);
      //   uart_printf("vpn : %x\n", vpn);
    }

    //indirect map
    else if(lpn < FILE_FTL_NUM * FILE_FTL_NUM * NUM_INDIRECT_MAP + FILE_FTL_NUM * NUM_DIRECT_MAP)
    {
        t_lpn = lpn - FILE_FTL_NUM * NUM_DIRECT_MAP;
        i = t_lpn / (FILE_FTL_NUM * FILE_FTL_NUM);
        t_lpn = t_lpn %  (FILE_FTL_NUM * FILE_FTL_NUM);
        j = t_lpn / FILE_FTL_NUM;
        k = t_lpn % FILE_FTL_NUM;
        
        if(inode->file_ftl_meta.indirect_mapped[i]==NULL \
        || inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]==NULL\
        || inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]->vpn[k] == NULL)
        {
         //   uart_print("2fail get vpn");
            return (UINT32)NULL;
        }    

        vpn = inode->file_ftl_meta.indirect_mapped[i]->next_mapped[j]->vpn[k];
    }
    //double indirect map
    else if (lpn < FILE_FTL_NUM * FILE_FTL_NUM * FILE_FTL_NUM * NUM_DOUBLE_INDIRECT_MAP\
     + FILE_FTL_NUM * FILE_FTL_NUM * NUM_INDIRECT_MAP + FILE_FTL_NUM * NUM_DIRECT_MAP)
    {
        t_lpn = lpn - ( FILE_FTL_NUM * FILE_FTL_NUM * NUM_INDIRECT_MAP + FILE_FTL_NUM * NUM_DIRECT_MAP);
        i = t_lpn / (FILE_FTL_NUM * FILE_FTL_NUM * FILE_FTL_NUM);
        t_lpn = t_lpn % (FILE_FTL_NUM * FILE_FTL_NUM * FILE_FTL_NUM);
        j = t_lpn / (FILE_FTL_NUM * FILE_FTL_NUM);
        t_lpn = t_lpn % (FILE_FTL_NUM * FILE_FTL_NUM);
        k = t_lpn / FILE_FTL_NUM;
        l = t_lpn % FILE_FTL_NUM;

        if(inode->file_ftl_meta.double_indirect_mapped[i]==NULL \
        || inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]==NULL\
        || inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k] == NULL\
        || inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k]->vpn[l] == NULL)
        {
            //uart_print("3fail get vpn");
            return (UINT32)NULL;
        }    

        vpn = inode->file_ftl_meta.double_indirect_mapped[i]->next_mapped[j]->next_mapped[k]->vpn[l];
    }
  //  uart_printf("//\n");
    //uart_printf("[get_vpn_] lpn : %x vpn : %x\n",lpn, vpn);
  //  uart_printf("get_vpn %x %x",lpn, vpn);

    return vpn;
}

/************** Test Function**************************/
//  
//
//
//////////////////////////////////////////////////////////////////////////
/*
void DS_test()
{
    int i,j;

    uart_printf("[DS_test]\n");
    //Can I use memory function?
    //YES!
    /*
    uart_printf("[DS_test]\n");
    char hi[12];
    memcpy(hi, "Hello World", 12);
    uart_printf("memcpy success? : %s\n", hi);
    for (i=0; i<12; i++)
    {
        uart_printf("%c-", hi[i]);
    }
    uart_printf("\n");
    uart_printf("memcmp success? %d\n", memcmp(hi, "Hello World", 11));
    uart_printf("strlen success? %d\n" , strlen(hi));
    */


/*
    UINT8 key[KEY_SIZE];
    char name[NAME_LEN];
   // int i;
    int fid[5];
    DS_init_superblock();
    //uart_printf("init success \n");
    memcpy(name, "foo.txt", 8);
    for(i=0; i<KEY_SIZE; i++)
    {
        key[i]=0xff;
    }
    //evaluation 1. 단일 파일 생성 후 쓰고 읽기.
    fid[0]=DS_file_create(name, key);
    fid[1]=DS_file_create("hi.txt", key);
    fid[2]=DS_file_create("hello.txt", key);
    fid[3]=DS_file_create("bye.txt", key);
 
    //u/art_printf("DS_test) fid : %d %d %d %d\n", fid[0], fid[1], fid[2], fid[3]);


    DS_print_all();
    
    //test ! memory test on FTL
    /*
    uart_printf("VA\n");
    for(i=0; i<16; i++)
    {
        for(j=0; j<16; j++)
            uart_printf("%x ", *(UINT32*)(DS_FTL_ADDR+4*(i*16+j)));
        uart_printf("\n");
    }
    uart_printf("PA\n");
    for(i=0; i<4; i++)
    {
        for(j=0; j<16; j++)
            uart_printf("%x ", *(UINT32*)DS_VA_to_PA((DS_FTL_ADDR+4*(i*16+j))));
        uart_printf("\n");
    }
    */
    //UINT32 fid2;
    //bitmap check!
    /*
    uart_printf("%x %x %x %x %x %x %x %x ", ds_superblock.ds_inode_bmp[0], \
                                            ds_superblock.ds_inode_bmp[1], \
                                            ds_superblock.ds_inode_bmp[2], \
                                            ds_superblock.ds_inode_bmp[3], \
                                            ds_superblock.ds_inode_bmp[4], \
                                            ds_superblock.ds_inode_bmp[5], \
                                            ds_superblock.ds_inode_bmp[6], \
                                            ds_superblock.ds_inode_bmp[7]);
    */
    //uart_printf("DS_test) : open/close");
   // uart_printf("close %s %d", name, fid[0]);
  /*
    DS_file_close(fid[0]);
    //uart_printf("closed");
    fid[0]=DS_file_open(name);
    uart_printf("what is fid? : %d", fid[0]);
    */
    //DS_file_close(fid2);
/*
    uart_printf("%d", fid2);

    fid2=DS_file_open("hi.txt");
    DS_file_close(fid2);
    uart_printf("%d", fid2);
    fid2=DS_file_open("bye.txt");
    DS_file_close(fid2);
    uart_printf("%d", fid2);
      fid2=DS_file_open(name);
    DS_file_close(fid2);
    uart_printf("%d", fid2);
    fid2=DS_file_open("hello.txt");
    DS_file_close(fid2);
    uart_printf("%d", fid2);
    */
   /*
    int e1,e2,e3;
    int k1,k2,k3;
    int enc_id, file_id;
    SGX_LBA sgx_lba;
    UINT32 vpn;
    int max_enc, max_file;
    UINT8 sym_key[KEY_SIZE];
    int i;
    for(i=0; i<KEY_SIZE; i++)
        sym_key[i]=0x11;
  
    uart_printf("sgx test()\n");

    sgx_init();

    max_enc = 1;
    max_file = 16;
    uart_printf("file open : enclave %d, file %d (%d*%d)\n",max_enc,max_file,max_enc,max_file);
    //있다 테스트 시도.SGX_file_search
    for(enc_id=1; enc_id<=max_enc; enc_id++)
    {
        for(file_id=1; file_id<=max_file; file_id++)
        {
            sgx_file_open(enc_id,file_id,1,sym_key);
        }

    }
    sgx_print_all();
*/
/*
    uart_printf("file write / read test!\n");
    for(enc_id=1; enc_id<=max_enc; enc_id++)
    {
        for(file_id=1; file_id<=max_file;file_id++)
        {
            sgx_lba.enclave_id = enc_id;
            sgx_lba.file_id = file_id;
            sgx_lba.addr.position = 0;
            sgx_tc_write_seq(sgx_lba, 100, NUM_PSECTORS_4KB);
            uart_printf("%d %d write/read\n",enc_id,file_id);
        }
    }
    
    uart_printf("file test finish!\n");
  */
    //sgx_file_open(1, 1, 1, 0x1111);
   // sgx_file_open(1, 2, 2, 0x2222);
    //sgx_file_open(2, 1, 1, 0x2222)
/*
//enclave public key table test
    uart_printf("1st enclve-public key table test\n");
    sgx_enclave_insert(1, 0x1111);
    sgx_enclave_insert(2, 0x2222);
    sgx_enclave_insert(3, 0x3333);
    
    
    e1=sgx_enclave_exists(2);
    e2=sgx_enclave_exists(4);
    e3=sgx_enclave_exists(3);

    k1=sgx_enclave_get_public_key(1);
    k2=sgx_enclave_get_public_key(2);
    k3=sgx_enclave_get_public_key(3);

    uart_printf("exists: %d %d %d\n", e1,e2,e3);
    uart_printf("key : %x %x %x\n",k1,k2,k3);

    uart_printf("2nd : enclave-file table test!\n");

    sgx_file_insert(1, 1);
    sgx_file_insert(1, 2);
    sgx_file_insert(2, 2);

 */   
/*
    uart_printf("test set_vpn / get_vpn\n");
    SGX_LBA sgx_lba;
    sgx_lba.enclave_id = 1;
    sgx_lba.file_id=1;
    sgx_lba.addr.lpn=111111111;

    uart_printf("set vpn : 0x2222\n");
    sgx_set_vpn(sgx_lba, 0x2222);

    vpn=sgx_get_vpn(sgx_lba);
    uart_printf("vpn : %x\n", vpn);
*/
 /*   
}

void DS_print_all()
{
    int i;
    INODE *inode;
    /*
    uart_printf("[DS_print_all]\n");
    
    uart_printf("\n1. Size Check \n");
    uart_printf("sgx_lba size(8) : %d\n",sizeof(SGX_LBA));
    uart_printf("INODE size(%d) : %d\n", DS_INODE_SIZE, sizeof(INODE));
    uart_printf("nhash size(%d) : %d\n", DS_NHASH_SIZE, sizeof(HASH_ELE));


    uart_printf("\n2. filetable check \n");
    int DBG_FID_SIZE = 10;
    int DBG_BUCKET_SIZE = BUCKET_SIZE;
    for(i=1; i<=DBG_FID_SIZE; i++)
    {
        inode = DS_filetb_get_inode(i);
        uart_printf("%d : innode addr(%x), key(%d), ino(%d)\n", i, \
        (UINT32)inode, inode->key, inode->no );
    }
    
    uart_printf("\n3. nhash check \n");
    for(i=1; i<=DBG_BUCKET_SIZE; i++)
    {
        if(ds_superblock.name_hash[i])
        {
            inode =  ds_superblock.name_hash[i]->inode;
            uart_printf("%d : ino(%d), name(%s)\n", i, inode->no, inode->name);
        }
    }
    */
   /*
}
*/
