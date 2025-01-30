#include "libraries.h"

#define MAX_FILES 100
#define MAX_ABILITIES 50
#define MAX_PLAYERS 50

char BASE_PATH[MAX_PATH];
char rec_file[MAX_PATH] = "";
FILETIME latest_time = {0, 0};
char c_files[MAX_FILES][MAX_PATH];
FILETIME f_times[MAX_FILES];
int file_count = 0;

typedef struct {
    char name[50];
    int total_damage;
    int ability_damage[MAX_ABILITIES];
    char ability_names[MAX_ABILITIES][50];
    int ability_count;
    time_t first_timestamp;
    time_t last_timestamp;
} pstats;

pstats players[MAX_PLAYERS];
int p_count = 0;

#define INFO(...) printf("[INFO] " __VA_ARGS__)
#define LOG_ERROR(...) printf("[ERROR] " __VA_ARGS__)
#define DEBUG(...) printf("[DEBUG] " __VA_ARGS__)


void init_bpath() {
    char local_appdata[MAX_PATH];
    if (GetEnvironmentVariable("LOCALAPPDATA", local_appdata, MAX_PATH) == 0) {
        LOG_ERROR("Unable to retrieve LOCALAPPDATA.\n");
        exit(1);
    }
    snprintf(BASE_PATH, MAX_PATH, "%s\\Temp\\Visionary Realms\\Pantheon", local_appdata);
    INFO("Base Path set to: %s\n", BASE_PATH);
}

void add_c_file(const char *file_path, FILETIME file_time) {
    if (file_count < MAX_FILES) {
        strcpy(c_files[file_count], file_path);
        f_times[file_count] = file_time;
        file_count++;
    } else {
        LOG_ERROR("Maximum file limit reached. Cannot add more files.\n");
    }
}

void timeforfile(FILETIME ft, char *buffer, size_t buffer_size) {
    SYSTEMTIME stUTC, stLocal;
    FileTimeToSystemTime(&ft, &stUTC);
    SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

    char am_pm[] = "AM";
    int hour = stLocal.wHour;
    if (hour >= 12) {
        am_pm[0] = 'P';
        if (hour > 12) hour -= 12;
    } else if (hour == 0) {
        hour = 12;
    }

    snprintf(buffer, buffer_size, "%02d/%02d/%04d - %d:%02d %s",
             stLocal.wMonth, stLocal.wDay, stLocal.wYear,
             hour, stLocal.wMinute, am_pm);
}

int comp_files(const void *a, const void *b) {
    FILETIME ft_a = f_times[*(int *)a];
    FILETIME ft_b = f_times[*(int *)b];
    return CompareFileTime(&ft_b, &ft_a);
}

void search_c_files(const char *base_path) {
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", base_path);

    WIN32_FIND_DATA find_data;
    HANDLE hFind = FindFirstFile(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_ERROR("No files found in directory: %s\n", base_path);
        return;
    }

    INFO("Searching directory: %s\n", base_path);

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
            continue;

        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", base_path, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            search_c_files(full_path);
        } else if (strcmp(find_data.cFileName, "Combat") == 0) {
            add_c_file(full_path, find_data.ftLastWriteTime);
            if (CompareFileTime(&find_data.ftLastWriteTime, &latest_time) > 0) {
                latest_time = find_data.ftLastWriteTime;
                strcpy(rec_file, full_path);
            }
        }
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);
}

void get_player(const char *message, char *player) {
    const char *dealt_pos = strstr(message, " dealt");
    if (dealt_pos) {
        size_t name_length = dealt_pos - message;
        strncpy(player, message, name_length);
        player[name_length] = '\0';
    } else {
        strcpy(player, "Unknown");
    }
}

int get_damage(const char *message) {
    while (*message) {
        if (isdigit(*message)) {
            return atoi(message);
        }
        message++;
    }
    return 0;
}

void get_abilitiy(const char *message, char *ability) {
    const char *with_pos = strstr(message, "with ");
    if (with_pos) {
        strcpy(ability, with_pos + 5);
        char *end = strchr(ability, '.');
        if (end) *end = '\0';
    } else {
        strcpy(ability, "Unknown");
    }
}

pstats *get_player_stats(const char *player) {
    for (int i = 0; i < p_count; i++) {
        if (strcmp(players[i].name, player) == 0) {
            return &players[i];
        }
    }
    if (p_count < MAX_PLAYERS) {
        strcpy(players[p_count].name, player);
        players[p_count].total_damage = 0;
        players[p_count].ability_count = 0;
        return &players[p_count++];
    }
    return NULL;
}

void add_ability(pstats *player, const char *ability, int damage) {
    for (int i = 0; i < player->ability_count; i++) {
        if (strcmp(player->ability_names[i], ability) == 0) {
            player->ability_damage[i] += damage;
            return;
        }
    }
    if (player->ability_count < MAX_ABILITIES) {
        strcpy(player->ability_names[player->ability_count], ability);
        player->ability_damage[player->ability_count] = damage;
        player->ability_count++;
    }
}

void proc_log(cJSON *json) {
    INFO("Parsing JSON file...\n");

    cJSON *messages = cJSON_GetObjectItemCaseSensitive(json, "Messages");
    if (!cJSON_IsArray(messages)) {
        LOG_ERROR("No valid messages found in the JSON, check the data for corruption or other problems.\n");
        return;
    }

    cJSON *message;
    cJSON_ArrayForEach(message, messages) {
        cJSON *msg_text = cJSON_GetObjectItemCaseSensitive(message, "Message");
        if (!cJSON_IsString(msg_text) || !msg_text->valuestring) continue;

        if (strstr(msg_text->valuestring, "dealt") && strstr(msg_text->valuestring, "damage")) {
            char player[50], ability[50];
            int damage = get_damage(msg_text->valuestring);

            get_player(msg_text->valuestring, player);
            get_abilitiy(msg_text->valuestring, ability);

            pstats *p = get_player_stats(player);
            if (p) {
                p->total_damage += damage;
                add_ability(p, ability, damage);
            }
        }
    }

    INFO("Combat Statistics:\n");
    for (int i = 0; i < p_count; i++) {
        pstats *p = &players[i];
        printf("\nPlayer: %s\n", p->name);
        printf("  Total Damage: %d\n", p->total_damage);
        printf("  Abilities:\n");
        for (int j = 0; j < p->ability_count; j++) {
            printf("   - %s: %d damage\n", p->ability_names[j], p->ability_damage[j]);
        }
    }
}

void pjson(const char *file_path) {
    INFO("Opening JSON file: %s\n", file_path);

    FILE *file = fopen(file_path, "r");
    if (!file) {
        LOG_ERROR("Error opening JSON file: %s\n", file_path);
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char *json_data = (char *)malloc(file_size + 1);
    if (!json_data) {
        LOG_ERROR("Memory allocation error.\n");
        fclose(file);
        return;
    }

    fread(json_data, 1, file_size, file);
    json_data[file_size] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(json_data);
    if (!json) {
        LOG_ERROR("Invalid JSON format in: %s\n", file_path);
        free(json_data);
        return;
    }

    proc_log(json);
    cJSON_Delete(json);
    free(json_data);
}

void dmenu() {
    printf("\nSelect an option:\n");
    printf("1. Automatically parse the latest Combat JSON file (by date modified)\n");
    printf("2. Manually select a Combat JSON file from the list\n");
    printf("3. Exit\n");
    printf("Enter your choice: ");
}

void input_file() {
    if (file_count == 0) {
        INFO("No JSON files named 'Combat' found.\n");
        return;
    }

    int indices[MAX_FILES];
    for (int i = 0; i < file_count; i++) indices[i] = i;
    qsort(indices, file_count, sizeof(int), comp_files);

    INFO("List of Combat JSON files (newest first):\n");
    for (int i = 0; i < file_count; i++) {
        int idx = indices[i];
        char date_modified[30];
        timeforfile(f_times[idx], date_modified, sizeof(date_modified));
        printf("%d. %s (Last Modified: %s)\n", i + 1, c_files[idx], date_modified);
    }

    printf("\nEnter the number of the file to parse (or 0 to cancel): ");
    int choice;
    if (scanf("%d", &choice) == 1 && choice > 0 && choice <= file_count) {
        int idx = indices[choice - 1];
        pjson(c_files[idx]);
    } else {
        INFO("File selection canceled!\n");
    }
}

int main() {
    init_bpath();
    search_c_files(BASE_PATH);

    while (1) {
        dmenu();
        int choice;
        if (scanf("%d", &choice) == 1) {
            switch (choice) {
                case 1:
                    if (strlen(rec_file) > 0) {
                        pjson(rec_file);
                    } else {
                        INFO("No JSON file named 'Combat' found....\n");
                    }
                    break;
                case 2:
                    input_file();
                    break;
                case 3:
                    INFO("Exiting the program, goodbye!\n");
                    return 0;
                default:
                    LOG_ERROR("Invalid choice, please try again\n");
            }
        } else {
            LOG_ERROR("Invalid input detected, please enter a valid number\n");
            while (getchar() != '\n');
        }
    }

    return 0;
}