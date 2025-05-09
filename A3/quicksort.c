#include "quicksort.h"


int main(int argc, char const *argv[])
{
	const char* input_file_name = "test.txt";
	const char* output_file_name = "result.txt";
	int* global_elements;
	int* elements;
	int n = 0;

	MPI_Init(&argc, &argv);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (rank == 0) n = read_input(input_file_name, &global_elements);
	MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

	int local_n = distribute_from_root(global_elements, n, &elements);
	serial_sort(elements, local_n);
	global_sort(&elements, n, MPI_COMM_WORLD, 1);

	return 0;
}


int check_and_print(int *elements, int n, char *file_name)
{
	if (!sorted_ascending(elements, n)) {
		printf("Error! Array failed to sort!\n");
		return -1;
	}

	FILE *file = fopen(file_name, "wb");
	if (file == NULL) {
		printf("Error! Can't open the file %s\n", file_name);
		return -2;
	}

	if (fwrite(&n, sizeof(int), 1, file) != 1) {
		fprintf(stderr, "Failed to write array size to file.\n");
		fclose(file);
		return -2;
	}

	if (fwrite(elements, sizeof(NUMBER), n, file) != (size_t)n) {
		fprintf(stderr, "Failed to write array data to file.\n");
		fclose(file);
		return -2;
	}

	fclose(file);
}


int global_sort(int **elements, int n, MPI_Comm comm, int pivot_strategy)
{
	int rank, size;
	MPI_Comm_rank(comm, &rank);
	MPI_Comm_size(comm, &size);

	// Require even number of processes for pairing
	if (size > 1 && size % 2 != 0) {
		if (rank == 0) fprintf(stderr, "global_sort: Number of processes must be even.\n");
		MPI_Abort(comm, 1);
	}

	// Base case: single process
	if (size == 1) {
		return n;
	}

	// 3.1 Select pivot on rank 0 and broadcast
	int pivot;
	if (rank == 0) {
		int pivot_index = select_pivot(pivot_strategy, *elements, n, comm);
		pivot = (*elements)[pivot_index];
	}
	MPI_Bcast(&pivot, 1, MPI_INT, 0, comm);

	// 3.2 Partition locally around pivot.
	// Use B-search to find the split point of the array.
	int left = 0, right = n;
	while (left < right) {
		int mid = left + (right - left) / 2;
		if ((*elements)[mid] < pivot) left = mid + 1;
		else right = mid;
	}
	const int left_arr_size  = left;
	const int right_arr_size = n - left;

	// 3.3 Split processes into two groups
	int color = (rank < size/2) ? 0 : 1;
	MPI_Comm sub_comm;
	MPI_Comm_split(comm, color, rank, &sub_comm);

	int partner_rank = (color == 0) ? rank + size/2 : rank - size/2;

	// Exchange sizes.
	int send_count = (color == 0) ? right_arr_size : left_arr_size;
	int recv_count;
	MPI_Sendrecv(&send_count, 1, MPI_INT, partner_rank, 0,
					&recv_count, 1, MPI_INT, partner_rank, 0,
					comm, MPI_STATUS_IGNORE);

	// Exchange data.
	int *recv_buff = malloc(recv_count * sizeof(int));
	const int *send_ptr = (color == 0) ? (*elements + left_arr_size) : *elements;
	MPI_Sendrecv(send_ptr, send_count, MPI_INT, partner_rank, 0,
					recv_buff, recv_count, MPI_INT, partner_rank, 0,
					comm, MPI_STATUS_IGNORE);

	// 3.4 Merge the two sorted runs into one sorted array.
	int new_n = (color == 0) ? left_arr_size : right_arr_size + recv_count;
	int *new_elements = malloc(new_n * sizeof(int));
	if (color == 0) {
		merge_ascending(*elements, left_arr_size,
						recv_buff, recv_count,
						new_elements);
	} else {
		merge_ascending(recv_buff, recv_count,
						*elements + left_arr_size, right_arr_size,
						new_elements);
	}

	free(*elements);
	free(recv_buff);
	*elements = new_elements;

	// 4 Recursive call.
	int final_n = global_sort(elements, new_n, sub_comm, pivot_strategy);
	MPI_Comm_free(&sub_comm);

	return final_n;
}

void merge_ascending(int *v1, int n1, int *v2, int n2, int *result)
{
	int i = 0, j = 0, k = 0;
	while (i < n1 && j < n2) {
		result[k++] = (v1[i] <= v2[j]) ? v1[i++] : v2[j++];
	}
	while (i < n1) result[k++] = v1[i++];
	while (j < n2) result[k++] = v2[j++];
}

int read_input(char *file_name, int **elements)
{
	FILE *file = fopen(file_name, "r");
	if (file == NULL) {
		fprintf(stderr, "Unable to read the input file %s!\n", file_name);
		return -2;
	}

	int n;
	if (fscanf(file, "%d", &n) != 1 || n < 1) {
		fprintf(stderr, "Failed to read array size\n");
		fclose(file);
		return -2;
	}

	*elements = malloc(n * sizeof(NUMBER));
	if (!*elements) {
		fprintf(stderr, "Error allocating memory for %d elements!\n", n);
		fclose(file);
		return -2;
	}

	for (int i = 0; i < n; i++) {
		if (fscanf(file, "%d", &(*elements)[i]) != 1) {
			fprintf(stderr, "Failed to read element %d\n", i);
			free(*elements);
			fclose(file);
			return -2;
		}
	}

	fclose(file);
	return n;
}

int sorted_ascending(int *elements, int n)
{
	for (int i=1; i<n; i++) {
		if (elements[i] < elements[i-1])
			return 0;
	}
	return 1;
}

void swap(int *e1, int *e2)
{
	int temp = *e1;
	*e1 = *e2;
	*e2 = temp;
}

void serial_sort(int *elements, int n)
{
	for (int i = 1; i < n; i++) {
		int key = elements[i];
		int j = i - 1;

		while (j >= 0 && elements[j] > key) {
			elements[j + 1] = elements[j];
			j--;
		}
		elements[j + 1] = key;
	}
}
