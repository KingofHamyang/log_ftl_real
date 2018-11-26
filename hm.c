//----------------------------------------------------------
//
// Project #4 : Log block FTL
// 	- Embedded Systems Design, ICE3028 (Fall, 2018)
//
// November. 22, 2018.
//
// Min-Woo Ahn, Dong-Hyun Kim (minwoo.ahn@csl.skku.edu, donghyun.kim@csl.skku.edu)
// Dong-Yun Lee (dongyun.lee@csl.skku.edu)
// Jin-Soo Kim (jinsookim@skku.edu)
// Computer Systems Laboratory
// Sungkyunkwan University
// http://csl.skku.edu/ICE3028S17
//
//---------------------------------------------------------

#include "hm.h"
#include "nand.h"

static void garbage_collection(void);

/***************************************
No restriction about return type and arguments in the following functions
***************************************/
static void switch_merge(int, int);
static void partial_merge(int, int);
static void full_merge(int, int);

int *L2B_data;
int *L2B_log;

int *invalid_count_per_block;
int *invalid_pages;
int *written_pages_per_block;
int *log_index;
int **log_PMT;

int spare_block = N_BLOCKS - 1;
int log_block_cnt = 0;

void ftl_open(void)
{
	L2B_data = (int *)malloc(sizeof(int) * N_USER_BLOCKS);
	L2B_log = (int *)malloc(sizeof(int) * N_USER_BLOCKS);

	invalid_count_per_block = (int *)malloc(sizeof(int) * N_BLOCKS);
	written_pages_per_block = (int *)malloc(sizeof(int) * N_BLOCKS);
	invalid_pages = (int *)malloc(sizeof(int) * N_PAGES_PER_BLOCK * N_BLOCKS);
	log_index = (int *)malloc(sizeof(int) * NUM_LOGBLOCK);

	log_PMT = (int **)malloc(sizeof(int *) * NUM_LOGBLOCK);
	for (int i = 0; i < NUM_LOGBLOCK; i++)
	{
		log_PMT[i] = (int *)malloc(sizeof(int) * N_PAGES_PER_BLOCK);
	}
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		L2B_data[i] = -1;
		L2B_log[i] = -1;
	}
	for (int i = 0; i < N_BLOCKS; i++)
	{
		written_pages_per_block[i] = 0;
		invalid_count_per_block[i] = 0;
	}
	for (int i = 0; i < N_BLOCKS * N_PAGES_PER_BLOCK; i++)
	{
		invalid_pages[i] = 0;
	}

	for (int i = 0; i < NUM_LOGBLOCK; i++)
	{
		for (int j = 0; j < N_PAGES_PER_BLOCK; j++)
		{
			log_PMT[i][j] = -1;
		}
	}
	for (int i = 0; i < NUM_LOGBLOCK; i++)
	{
		log_index[i] = -1;
	}
	nand_init(N_BLOCKS, N_PAGES_PER_BLOCK);

	return;
}

void ftl_read(long lpn, u32 *read_buffer)
{
	int block_n;
	int index;

	u32 spare;
	block_n = (int)lpn / N_PAGES_PER_BLOCK;
	index = (int)lpn % N_PAGES_PER_BLOCK;

	if (L2B_log[block_n] == -1)
	{
		//	printf("real read\n");
		if (nand_read(L2B_data[block_n], index, read_buffer, &spare) == -1)
		{
			printf("read 1error at real error");
		}
		return;
	}
	else
	{
		int P_Block = L2B_data[block_n];
		if (invalid_pages[P_Block * N_PAGES_PER_BLOCK + index] == 0)
		{
			//	printf("real read1\n");
			if (nand_read(P_Block, index, read_buffer, &spare) == -1)
			{
				printf("read 2error at real error");
			}
			return;
		}
		else
		{
			int log_mem = 0;
			int log_block = L2B_log[block_n];
			for (int i = 0; i < NUM_LOGBLOCK; i++)
			{
				if (log_index[i] == log_block)
				{
					log_mem = i;
					break;
				}
			}
			//	printf("real read\n");
			if (nand_read(log_block, log_PMT[log_mem][index], read_buffer, &spare) == -1)
			{
				printf("read 3error at real error %d ^ %d ", index, log_mem);
			}
			return;
		}
	}
	return;
}

void initialize_log_PMT(int a)
{
	for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
	{
		log_PMT[a][i] = -1;
	}
	return;
}

void ftl_write(long lpn, u32 *write_buffer)
{
	int block_n = (int)lpn / N_PAGES_PER_BLOCK;
	int index = (int)lpn % N_PAGES_PER_BLOCK;
	int P_Block = L2B_data[block_n];
	int log_Block = L2B_log[block_n];
	int log_mem;
	u32 spare = 1;
	//data_block allocate
	if (L2B_data[block_n] == -1)
	{

		for (int i = 0; i < N_BLOCKS; i++)
		{
			if (written_pages_per_block[i] == 0 && i != spare_block)
			{
				P_Block = i;
				break;
			}
		}
		L2B_data[block_n] = P_Block;
		//printf("write %d ", P_Block);
	}

	//write start

	if (index == written_pages_per_block[P_Block])
	{
		//printf("write %d ", P_Block);
		nand_write(P_Block, index, *write_buffer, spare);
		written_pages_per_block[P_Block]++;
		return;
	}
	else if (index > written_pages_per_block[P_Block])
	{
		//printf("write %d ", P_Block);
		for (int i = written_pages_per_block[P_Block]; i <= index; i++)
		{
			//printf("writeasdfsa %d ", P_Block);
			nand_write(P_Block, i, *write_buffer, spare);
			written_pages_per_block[P_Block]++;
		}
		return;
	}
	else //if log needed
	{

		////log block allocate
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + index] = 1;
	NEW:
		if (L2B_log[block_n] == -1)
		{
			if (log_block_cnt >= NUM_LOGBLOCK)
			{
				garbage_collection();
			}
			for (int i = 0; i < N_BLOCKS; i++)
			{
				if (written_pages_per_block[i] == 0 && i != spare_block)
				{
					log_Block = i;
					L2B_log[block_n] = log_Block;
					log_block_cnt++;
					break;
				}
			}
			for (int i = 0; i < N_USER_BLOCKS; i++)
			{
				if (log_index[i] == -1)
				{
					log_index[i] = log_Block;
					log_mem = i;
					initialize_log_PMT(log_mem);
					break;
				}
			}
		}
		if (log_Block == -1)
		{
			//printf("ffff");
			garbage_collection();
			for (int i = 0; i < N_BLOCKS; i++)
			{
				if (written_pages_per_block[i] == 0 && i != spare_block)
				{
					log_Block = i;
					L2B_log[block_n] = log_Block;
					log_block_cnt++;
					break;
				}
			}
			for (int i = 0; i < N_USER_BLOCKS; i++)
			{
				if (log_index[i] == -1)
				{
					log_index[i] = log_Block;
					log_mem = i;

					initialize_log_PMT(log_mem);
					break;
				}
			}
		}
		for (int i = 0; i < N_USER_BLOCKS; i++)
		{
			if (log_index[i] == log_Block)
			{
				log_mem = i;
				break;
			}
		}
		if (written_pages_per_block[log_Block] == N_PAGES_PER_BLOCK)
		{
			int cnt = 0;
			for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
			{
				if (log_PMT[log_mem][i] != i)
				{
					cnt = 1;
					break;
				}
			}
			if (cnt == 0)
			{
				switch_merge(P_Block, log_Block);
			}
			else
			{
				full_merge(P_Block, log_Block);
			}
			goto NEW;
		}

		//	printf("%d %d %d", P_Block, log_mem, index);
		nand_write(log_Block, written_pages_per_block[log_Block], *write_buffer, spare);

		if (log_PMT[log_mem][index] != -1)
		{
			invalid_pages[log_Block * N_PAGES_PER_BLOCK + log_PMT[log_mem][index]] = 1;
		}
		log_PMT[log_mem][index] = written_pages_per_block[log_Block];
		written_pages_per_block[log_Block]++;
	}

	return;
}

static void switch_merge(int P_Block, int log_Block)
{
	//printf("asdfsadf ");
	int origin = 0;
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		if (L2B_data[i] == P_Block)
		{
			origin = i;
			break;
		}
	}
	L2B_log[origin] = log_Block;
	nand_erase(P_Block);
	for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
	{
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + i] = 0;
	}
	written_pages_per_block[P_Block] = 0;

	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		if (log_index[i] == log_Block)
		{
			log_index[i] = -1;
			break;
		}
	}
	log_block_cnt--;
}
static void partial_merge(int P_Block, int log_Block)
{
	u32 data;
	u32 spare;
	//printf("writeasdfsa %d ", P_Block);
	int start_copy_point = written_pages_per_block[log_Block];
	for (int i = start_copy_point; i < written_pages_per_block[P_Block]; i++)
	{
		nand_read(P_Block, i, &data, &spare);
		nand_write(log_Block, i, data, spare);
		written_pages_per_block[log_Block]++;
	}

	int origin = 0;
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		if (L2B_data[i] == P_Block)
		{
			origin = i;
			break;
		}
	}
	L2B_log[origin] = log_Block;
	nand_erase(P_Block);
	for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
	{
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + i] = 0;
	}
	written_pages_per_block[P_Block] = 0;

	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		if (log_index[i] == log_Block)
		{
			log_index[i] = -1;
			break;
		}
	}
	log_block_cnt--;
}
static void full_merge(int P_Block, int log_Block)
{

	//printf("asdfsadf ");
	u32 data;
	u32 spare;
	int origin;
	int log_mem;
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		if (L2B_data[i] == P_Block)
		{
			origin = i;
			break;
		}
	}
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		if (log_index[i] == log_Block)
		{
			log_mem = i;
			break;
		}
	}
	written_pages_per_block[spare_block] = 0;
	for (int i = 0; i < written_pages_per_block[P_Block]; i++)
	{
		if (invalid_pages[P_Block * N_PAGES_PER_BLOCK + i] == 0)
		{
			nand_read(P_Block, i, &data, &spare);
			nand_write(spare_block, i, data, spare);
			written_pages_per_block[spare_block]++;
		}
		else
		{
			nand_read(log_Block, log_PMT[log_mem][i], &data, &spare);
			nand_write(spare_block, i, data, spare);
			written_pages_per_block[spare_block]++;
		}
		s.gc_write++;
	}
	nand_erase(P_Block);
	nand_erase(log_Block);
	written_pages_per_block[P_Block] = 0;
	written_pages_per_block[log_Block] = 0;
	for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
	{
		invalid_pages[log_Block * N_PAGES_PER_BLOCK + i] = 0;
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + i] = 0;
	}
	log_index[log_mem] = -1;
	L2B_data[origin] = spare_block;
	L2B_log[origin] = -1;
	spare_block = P_Block;
}

static void garbage_collection(void)
/***************************************
You can add some arguments and change
return type to anything you want
***************************************/
{
	int victim_data;
	int victim_log;
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		victim_data = L2B_data[i];
		victim_log = L2B_log[i];
		if (victim_data != -1 && victim_log != -1)
		{
			full_merge(victim_data, victim_log);
		}
	}

	s.gc++;

	/***************************************
Add

s.gc_write++;

for every nand_write call (every valid page copy)
that you issue in this function
***************************************/

	return;
}
