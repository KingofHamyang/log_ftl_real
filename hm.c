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
static int switch_merge(int, int);
static int partial_merge(int, int);
static int full_merge(int, int, int);

int full_merge_cnt = 0;

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
	log_index = (int *)malloc(sizeof(int) * (NUM_LOGBLOCK - 1));

	log_PMT = (int **)malloc(sizeof(int *) * (NUM_LOGBLOCK - 1));
	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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

	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
	{
		for (int j = 0; j < N_PAGES_PER_BLOCK; j++)
		{
			log_PMT[i][j] = -1;
		}
	}
	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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
			for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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

	NEW:
		if (L2B_log[block_n] == -1)
		{
			int flag2 = 0;
			if (log_block_cnt >= NUM_LOGBLOCK - 1)
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
					flag2 = 1;
					break;
				}
			}
			if (flag2 == 1)
			{
				for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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
			//TODO -> 밑에꺼도 예외처리, lgo block 갯수 컨트롤
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
			for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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
		for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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
				P_Block = switch_merge(P_Block, log_Block);
			}
			else
			{
				P_Block = full_merge(P_Block, log_Block, 0);
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
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + index] = 1;
	}

	return;
}

static int switch_merge(int P_Block, int log_Block)
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
	L2B_data[origin] = log_Block;
	nand_erase(P_Block);
	for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
	{
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + i] = 0;
	}
	written_pages_per_block[P_Block] = 0;

	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
	{
		if (log_index[i] == log_Block)
		{
			log_index[i] = -1;
			break;
		}
	}
	log_block_cnt--;
	return L2B_data[origin];
}
static int partial_merge(int P_Block, int log_Block)
{
	u32 data;
	u32 spare;
	//printf("writeasdfsa %d ", P_Block);
	int start_copy_point = written_pages_per_block[log_Block];
	for (int i = start_copy_point; i < written_pages_per_block[P_Block]; i++)
	{
		nand_read(P_Block, i, &data, &spare);
		nand_write(log_Block, i, data, spare);
		s.gc_write++;
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

	L2B_data[origin] = log_Block;
	nand_erase(P_Block);
	for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
	{
		invalid_pages[P_Block * N_PAGES_PER_BLOCK + i] = 0;
	}
	written_pages_per_block[P_Block] = 0;

	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
	{
		if (log_index[i] == log_Block)
		{
			log_index[i] = -1;
			break;
		}
	}
	log_block_cnt--;
	return L2B_data[origin];
}
static int full_merge(int P_Block, int log_Block, int flag)
{
	full_merge_cnt++;

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
	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
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
			if (nand_read(P_Block, i, &data, &spare) == -1)
			{
				printf("full merge data read error\n");
			}
			nand_write(spare_block, i, data, spare);
			written_pages_per_block[spare_block]++;
		}
		else
		{
			if (nand_read(log_Block, log_PMT[log_mem][i], &data, &spare) == -1)
			{
				for (int i = 0; i < N_PAGES_PER_BLOCK; i++)
				{
					printf("%d	%d %d %d\n", invalid_pages[P_Block * N_PAGES_PER_BLOCK + i], log_PMT[0][i], log_PMT[1][i], log_PMT[2][i]);
				}
				printf("full merge log read error\n %d %d %d %d %d", written_pages_per_block[log_Block], i, log_mem, full_merge_cnt, flag);
			}
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
	log_block_cnt--;
	return L2B_data[origin];
}

static void garbage_collection(void)
/***************************************
You can add some arguments and change
return type to anything you want
***************************************/
{
	s.gc++;
	int victim_data;
	int victim_log;
	for (int i = 0; i < N_USER_BLOCKS; i++)
	{
		victim_data = L2B_data[i];
		victim_log = L2B_log[i];
		if (victim_data != -1 && victim_log != -1)
		{

			full_merge(victim_data, victim_log, 1);
			return;
		}
	}
}

int can_be_switch(int P_Block, int log_Block)
{
	int log_mem = 0;
	for (int i = 0; i < NUM_LOGBLOCK - 1; i++)
	{
		if (log_index[i] == log_Block)
		{
			log_mem = i;
			break;
		}
	}
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
		return 1;
	}
	else
	{
		return 0;
	}
}

int can_be_partial(int P_Block, int log_Block)
{
}
