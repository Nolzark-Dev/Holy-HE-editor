#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include "syntax_highlight.h"

#define MAX_LINES 1000
#define MAX_LINE_LENGTH 1000
#define MAX_FILENAME 256
#define MAX_CLIPBOARD 10000
#define MAX_COMPLETIONS 100
#define MAX_COMPLETION_LENGTH 50
#define LINE_NUMBER_WIDTH 4  // Width of the line number column

char* lines[MAX_LINES];
int num_lines = 0;
int cursor_x = 0, cursor_y = 0;
int top_line = 0;
char filename[MAX_FILENAME];
char clipboard[MAX_CLIPBOARD];
int clipboard_start = -1;
int clipboard_end = -1;
char mode = 'n'; // 'n' for normal, 'i' for insert
char status_message[MAX_LINE_LENGTH];

char* completions[MAX_COMPLETIONS];
int num_completions = 0;
int current_completion = -1;

void init_editor() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_WHITE, COLOR_YELLOW);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_syntax_highlight();
}

void cleanup() {
    for (int i = 0; i < num_lines; i++) {
        free(lines[i]);
    }
    for (int i = 0; i < num_completions; i++) {
        free(completions[i]);
    }
    endwin();
}

void load_file() {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        lines[0] = strdup("");
        num_lines = 1;
        return;
    }

    char buffer[MAX_LINE_LENGTH];
    while (fgets(buffer, sizeof(buffer), file) != NULL && num_lines < MAX_LINES) {
        buffer[strcspn(buffer, "\n")] = 0;
        lines[num_lines++] = strdup(buffer);
    }

    fclose(file);
}

void save_file() {
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        snprintf(status_message, sizeof(status_message), "error: save unsuccessful");
        return;
    }

    for (int i = 0; i < num_lines; i++) {
        fprintf(file, "%s\n", lines[i]);
    }

    fclose(file);
    snprintf(status_message, sizeof(status_message), "saved successfully");
}

void insert_char(char c) {
    int len = strlen(lines[cursor_y]);
    if (len < MAX_LINE_LENGTH - 1) {
        memmove(&lines[cursor_y][cursor_x + 1], &lines[cursor_y][cursor_x], len - cursor_x + 1);
        lines[cursor_y][cursor_x] = c;
        cursor_x++;
    }
}

void delete_char() {
    int len = strlen(lines[cursor_y]);
    if (cursor_x < len) {
        memmove(&lines[cursor_y][cursor_x], &lines[cursor_y][cursor_x + 1], len - cursor_x);
    } else if (cursor_y < num_lines - 1) {
        strcat(lines[cursor_y], lines[cursor_y + 1]);
        free(lines[cursor_y + 1]);
        memmove(&lines[cursor_y + 1], &lines[cursor_y + 2], (num_lines - cursor_y - 2) * sizeof(char*));
        num_lines--;
    }
}

void insert_line() {
    if (num_lines < MAX_LINES - 1) {
        memmove(&lines[cursor_y + 2], &lines[cursor_y + 1], (num_lines - cursor_y - 1) * sizeof(char*));
        lines[cursor_y + 1] = strdup("");
        num_lines++;
        cursor_y++;
        cursor_x = 0;
    }
}

void draw_screen() {
    clear();

    int max_lines = LINES - 2; // Reserve bottom line for navbar
    for (int i = 0; i < max_lines && i + top_line < num_lines; i++) {
        move(i, 0);
        
        // Draw line numbers
        attron(COLOR_PAIR(2));  // Use color pair 2 for line numbers
        mvprintw(i, 0, "%*d ", LINE_NUMBER_WIDTH, i + top_line + 1);
        attroff(COLOR_PAIR(2));
        
        char* current_line = lines[i + top_line];
        
        int line_length = strcspn(current_line, "\n");
        
        // Create a temporary buffer to hold the line without the newline
        char temp_line[MAX_LINE_LENGTH];
        strncpy(temp_line, current_line, line_length);
        temp_line[line_length] = '\0';  // Null-terminate the string
        
        // Apply syntax highlighting to the line
        highlight_syntax(stdscr, temp_line);
        
        clrtoeol();
    }

    // bottom navbar
    attron(COLOR_PAIR(1));
    mvhline(LINES - 2, 0, ' ', COLS);
    mvprintw(LINES - 2, 0, "%-*s", COLS - 40, filename);
    mvprintw(LINES - 2, COLS - 40, "%s | %d,%d", mode == 'n' ? "EDEN" : "LODGE", cursor_y + 1, cursor_x + 1);
    attroff(COLOR_PAIR(1));

    // status message
    attron(COLOR_PAIR(2));
    mvhline(LINES - 1, 0, ' ', COLS);
    mvprintw(LINES - 1, 0, "%s", status_message);
    attroff(COLOR_PAIR(2));

    // Adjust cursor position to account for line numbers
    move(cursor_y - top_line, cursor_x + LINE_NUMBER_WIDTH + 1);
    refresh();
}

void handle_exit(int save) {
    if (save) {
        save_file();
    }
    cleanup();
    endwin();
    exit(0);
}

void copy_lines(int start, int end) {
    clipboard[0] = '\0';
    for (int i = start; i <= end && i < num_lines; i++) {
        if (strlen(clipboard) + strlen(lines[i]) + 2 < MAX_CLIPBOARD) {
            strcat(clipboard, lines[i]);
            strcat(clipboard, "\n");
        }
    }
    clipboard_start = start;
    clipboard_end = end;
    snprintf(status_message, sizeof(status_message), "Copied lines %d to %d", start + 1, end + 1);
}

void paste_lines() {
    if (clipboard_start == -1 || clipboard_end == -1) {
        snprintf(status_message, sizeof(status_message), "Nothing to paste");
        return;
    }

    char* token = strtok(clipboard, "\n");
    while (token != NULL) {
        if (num_lines < MAX_LINES - 1) {
            memmove(&lines[cursor_y + 2], &lines[cursor_y + 1], (num_lines - cursor_y - 1) * sizeof(char*));
            lines[cursor_y + 1] = strdup(token);
            num_lines++;
            cursor_y++;
        }
        token = strtok(NULL, "\n");
    }
    snprintf(status_message, sizeof(status_message), "Pasted %d lines", clipboard_end - clipboard_start + 1);
}

void delete_line() {
    if (num_lines > 1) {
        free(lines[cursor_y]);
        memmove(&lines[cursor_y], &lines[cursor_y + 1], (num_lines - cursor_y - 1) * sizeof(char*));
        num_lines--;
        if (cursor_y == num_lines) {
            cursor_y--;
        }
        cursor_x = 0;
        snprintf(status_message, sizeof(status_message), "Deleted line %d", cursor_y + 1);
    } else {
        lines[0] = strdup("");
        cursor_x = 0;
        snprintf(status_message, sizeof(status_message), "Deleted line content");
    }
}

void handle_normal_mode(int ch) {
    static int last_ch = 0;
    
    if (last_ch == 'd' && ch == 'd') {
        delete_line();
        last_ch = 0;
    } else {
        switch (ch) {
            case 'i': 
                mode = 'i'; 
                snprintf(status_message, sizeof(status_message), "--+ LODGE +--");
                break;
            case 'h': if (cursor_x > 0) cursor_x--; break;
            case 'j': if (cursor_y < num_lines - 1) cursor_y++; break;
            case 'u': if (cursor_y > 0) cursor_y--; break;
            case 'k': if (cursor_x < strlen(lines[cursor_y])) cursor_x++; break;
            case ':':
                mvprintw(LINES - 1, 0, ":");
                echo();
                char command[20];
                getstr(command);
                noecho();
                if (strcmp(command, "w") == 0) save_file();
                else if (strcmp(command, "q") == 0) handle_exit(0);
                else if (strncmp(command, "c", 1) == 0) {
                    int start = cursor_y, end = cursor_y;
                    if (sscanf(command + 1, "%d %d", &start, &end) == 2) {
                        copy_lines(start - 1, end - 1);  // Convert to 0-based index
                    } else {
                        copy_lines(cursor_y, cursor_y);
                    }
                }
                else if (strcmp(command, "v") == 0) {
                    paste_lines();
                }
                break;
            case 27: // ESC key
                clear();
                handle_exit(1); // Save exit
                break;
        }
        last_ch = ch;
    }
}

void handle_insert_mode(int ch) {
    switch (ch) {
        case 27: // ESC key
            mode = 'n'; 
            snprintf(status_message, sizeof(status_message), "");
            if (cursor_x > 0) cursor_x--;
            break;
        case KEY_BACKSPACE:
        case 127:
            if (cursor_x > 0) {
                memmove(&lines[cursor_y][cursor_x - 1], &lines[cursor_y][cursor_x], strlen(lines[cursor_y]) - cursor_x + 1);
                cursor_x--;
            } else if (cursor_y > 0) {
                cursor_x = strlen(lines[cursor_y - 1]);
                strcat(lines[cursor_y - 1], lines[cursor_y]);
                free(lines[cursor_y]);
                memmove(&lines[cursor_y], &lines[cursor_y + 1], (num_lines - cursor_y - 1) * sizeof(char*));
                num_lines--;
                cursor_y--;
            }
            break;
        case KEY_DC:
            delete_char();
            break;
        case KEY_ENTER:
        case 10:
            insert_line();
            break;
        case KEY_LEFT:
            if (cursor_x > 0) cursor_x--;
            break;
        case KEY_RIGHT:
            if (cursor_x < strlen(lines[cursor_y])) cursor_x++;
            break;
        case KEY_UP:
            if (cursor_y > 0) cursor_y--;
            break;
        case KEY_DOWN:
            if (cursor_y < num_lines - 1) cursor_y++;
            break;
        default:
            if (isprint(ch)) {
                insert_char(ch);
            }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("     ___\n");
        printf("    /\\  \\\n");
        printf("    \\:\\  \\      ___       Commands:           Navigation:\n");
        printf("     \\:\\  \\    /\\__\\        :q  quit\n");
        printf(" ___ /::\\  \\  /:/__/        :w  write            k\n");
        printf("/\\  /:/\\:\\__\\/::\\  \\        dd  delete line    h l  or arrow keys\n");
        printf("\\:\\/:/  \\/__/\\/\\:\\  \\__     esc exit lodge       j\n");
        printf(" \\::/__/      ~~\\:\\/\\__\\    esc save+exit\n");
        printf("  \\:\\  \\         \\::/  /    shift+esc abort\n");
        printf("   \\:\\__\\        /:/  /\n");
        printf("    \\/__/        \\/__/    Modes:\n");
        printf("                            eden: i.e command/normal mode\n");
        printf("  Welcome to the HI TE      lodge: i.e insert/write mode\n");
        return 1;
    }

    strncpy(filename, argv[1], sizeof(filename) - 1);
    init_editor();
    load_file();

    int ch;
    while (1) {
        draw_screen();
        ch = getch();

        if (ch == KEY_RESIZE) {
            // terminal resize
            clear();
            refresh();
            continue;
        }

        if (ch == 27) { // ESC key
            nodelay(stdscr, TRUE);
            int next_ch = getch();
            nodelay(stdscr, FALSE);
            
            if (next_ch == ERR) {
                // Only ESC 
                if (mode == 'i') {
                    mode = 'n';
                    snprintf(status_message, sizeof(status_message), "");
                    if (cursor_x > 0) cursor_x--;
                } else {
                    handle_exit(1); // Save and exit
                }
            } else if (next_ch == 27) {
                // Shift+ESC was pressed 
                clear();
                handle_exit(0); // Exit without saving
            }
        } else if (mode == 'n') {
            handle_normal_mode(ch);
        } else if (mode == 'i') {
            handle_insert_mode(ch);
        }

        if (cursor_y < top_line) top_line = cursor_y;
        if (cursor_y >= top_line + LINES - 1) top_line = cursor_y - LINES + 2;
    }

    clear();
    return 0;
}
