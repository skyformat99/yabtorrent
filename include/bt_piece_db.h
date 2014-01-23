/* piece database */
typedef struct
{
    int pass;
} bt_piecedb_t;

bt_piecedb_t *bt_piecedb_new();

void bt_piecedb_set_piece_length(bt_piecedb_t * db, const int piece_length_bytes);

void bt_piecedb_set_tot_file_size(bt_piecedb_t * db, const int tot_file_size_bytes);

int bt_piecedb_get_tot_file_size(bt_piecedb_t * db);

void bt_piecedb_set_diskstorage(bt_piecedb_t * db,
                                bt_blockrw_i * irw, void *udata);

void* bt_piecedb_get_diskstorage(bt_piecedb_t * db);

void *bt_piecedb_poll_best_from_bitfield(void * dbo, void * bf_possibles);

void *bt_piecedb_get(void* dbo, const unsigned int idx);

int bt_piecedb_add(bt_piecedb_t * db, const char *sha1, unsigned int size);

int bt_piecedb_get_length(bt_piecedb_t * db);

void bt_piecedb_print_pieces_downloaded(bt_piecedb_t * db);

void bt_piecedb_increase_piece_space(bt_piecedb_t* db, const int size);
