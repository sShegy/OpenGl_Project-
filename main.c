#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

// --- Definicije ---
#define MAX_ASTEROIDS 50
#define MAX_BULLETS 100
#define MAX_STARS 300
#define NUM_LAYERS 3
#define MISSED_ASTEROID_LIMIT 10
#define LEADERBOARD_SIZE 100

// --- Šejderi ---
const char* vertexShaderSource = "#version 330 core\nlayout (location = 0) in vec2 aPos; uniform vec2 u_Translate; uniform vec2 u_Scale; uniform float u_Rotation; void main() { mat2 rot = mat2(cos(u_Rotation), -sin(u_Rotation), sin(u_Rotation), cos(u_Rotation)); vec2 pos = rot * aPos; pos = pos * u_Scale; pos = pos + u_Translate; gl_Position = vec4(pos, 0.0, 1.0); }\0";
const char* fragmentShaderSource = "#version 330 core\n out vec4 FragColor; uniform vec3 u_Color; void main() { FragColor = vec4(u_Color, 1.0f); }\n\0";

// --- Strukture ---
typedef struct { float x, y; } Vec2;
typedef struct { float r, g, b; } Vec3;
typedef struct { Vec2 position; Vec2 size; Vec2 velocity; Vec3 color; float rotation; int active; } GameObject;
typedef struct { Vec2 position; float speed; int layer; } Star;
typedef struct { int score; char timestamp[30]; } LeaderboardEntry;

// --- Globalno stanje ---
unsigned int shaderProgram;
GameObject player;
GameObject asteroids[MAX_ASTEROIDS];
GameObject bullets[MAX_BULLETS];
Star stars[MAX_STARS];
LeaderboardEntry leaderboard[LEADERBOARD_SIZE];
int leaderboard_count = 0;
int score = 0;
int game_over = 0;
double asteroid_spawn_timer = 0.0, shoot_cooldown = 0.0;
int asteroids_missed = 0, missed_asteroids_rule_enabled = 1;
double game_over_animation_timer = 0.0;

// Boja asteroida: ciklus nijanse (HSV) za "vibriranje" boja tokom pada
static float asteroid_hue[MAX_ASTEROIDS];
static float asteroid_hue_speed[MAX_ASTEROIDS];

// --- Prototipovi funkcija ---
void initialize_game();
void save_leaderboard();

// HSV -> RGB konverzija (h,s,v u [0,1])
static inline Vec3 hsv_to_rgb(float h, float s, float v) {
    if (s <= 0.0f) return (Vec3){v, v, v};
    h = fmodf(h, 1.0f); if (h < 0.0f) h += 1.0f;
    float hf = h * 6.0f;
    int i = (int)floorf(hf);
    float f = hf - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: return (Vec3){v, t, p};
        case 1: return (Vec3){q, v, p};
        case 2: return (Vec3){p, v, t};
        case 3: return (Vec3){p, q, v};
        case 4: return (Vec3){t, p, v};
        default: return (Vec3){v, p, q};
    }
}

// --- Funkcije za Leaderboard ---
void print_full_leaderboard() {
    printf("\n--- KOMPLETAN LEADERBOARD ---\n");
    for (int i = 0; i < leaderboard_count; i++) printf("%d. %d poena (%s)\n", i + 1, leaderboard[i].score, leaderboard[i].timestamp);
    printf("---------------------------\n"); fflush(stdout);
}
int compare_scores(const void* a, const void* b) { return ((LeaderboardEntry*)b)->score - ((LeaderboardEntry*)a)->score; }
void load_leaderboard() {
    FILE* file = fopen("leaderboard.txt", "r"); if (file == NULL) return;
    leaderboard_count = 0;
    while (leaderboard_count < LEADERBOARD_SIZE && fscanf(file, "%d %[^\n]", &leaderboard[leaderboard_count].score, leaderboard[leaderboard_count].timestamp) == 2) leaderboard_count++;
    fclose(file); qsort(leaderboard, leaderboard_count, sizeof(LeaderboardEntry), compare_scores);
}
void save_leaderboard() {
    FILE* file = fopen("leaderboard.txt", "w"); if (file == NULL) return;
    for (int i = 0; i < leaderboard_count; i++) fprintf(file, "%d %s\n", leaderboard[i].score, leaderboard[i].timestamp);
    fclose(file);
}
void add_score_to_leaderboard(int new_score) {
    if (leaderboard_count < LEADERBOARD_SIZE || (leaderboard_count > 0 && new_score > leaderboard[leaderboard_count - 1].score)) {
        time_t t = time(NULL); struct tm* tm_info = localtime(&t);
        int index_to_add = (leaderboard_count < LEADERBOARD_SIZE) ? leaderboard_count++ : LEADERBOARD_SIZE - 1;
        leaderboard[index_to_add].score = new_score;
        strftime(leaderboard[index_to_add].timestamp, 30, "%Y-%m-%d %H:%M:%S", tm_info);
        qsort(leaderboard, leaderboard_count, sizeof(LeaderboardEntry), compare_scores);
        save_leaderboard();
    }
}

// --- Inicijalizacija igre ---
void initialize_game() {
    // Reset globalnog stanja
    score = 0;
    game_over = 0;
    asteroid_spawn_timer = 0.0;
    shoot_cooldown = 0.0;
    asteroids_missed = 0;
    game_over_animation_timer = 0.0;

    // Igrac
    player.position = (Vec2){0.0f, -0.8f};
    player.size = (Vec2){0.12f, 0.12f};
    player.velocity = (Vec2){1.5f, 0.0f};
    player.color = (Vec3){0.2f, 0.8f, 1.0f};
    player.rotation = 0.0f;
    player.active = 1;

    // Metci i asteroidi
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = 0;
        bullets[i].rotation = 0.0f;
        bullets[i].size = (Vec2){0.02f, 0.05f};
        bullets[i].color = (Vec3){1.0f, 1.0f, 0.0f};
    }
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        asteroids[i].active = 0;
        asteroids[i].rotation = 0.0f;
        asteroids[i].size = (Vec2){0.1f, 0.1f};
        // Inicijalne vrednosti za ciklus boje; konkretna boja se postavlja pri spawnu
        asteroid_hue[i] = (rand() % 1000) / 1000.0f;
        asteroid_hue_speed[i] = 0.15f + ((rand() % 200) / 1000.0f); // 0.15..0.35
        asteroids[i].color = hsv_to_rgb(asteroid_hue[i], 0.85f, 0.95f);
    }

    // Zvezde (paralaksa)
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].position = (Vec2){ ((rand() % 2000) / 1000.0f) - 1.0f, ((rand() % 2000) / 1000.0f) - 1.0f };
        stars[i].layer = rand() % NUM_LAYERS;
        float base = 0.15f;
        if (stars[i].layer == 0) stars[i].speed = base * 0.6f;
        else if (stars[i].layer == 1) stars[i].speed = base * 1.0f;
        else stars[i].speed = base * 1.5f;
    }
}

// --- Funkcije za igru ---
void shoot_bullet() { for (int i = 0; i < MAX_BULLETS; i++) if (!bullets[i].active) { bullets[i] = (GameObject){player.position, {0.02f, 0.05f}, {0.0f, 4.0f}, {1.0f, 1.0f, 0.0f}, 0.0f, 1}; return; } }
void spawn_asteroid() { for (int i = 0; i < MAX_ASTEROIDS; i++) if (!asteroids[i].active) { float size = ((rand() % 5) / 100.0f) + 0.08f; int idx = i; asteroid_hue[idx] = (rand() % 1000) / 1000.0f; asteroid_hue_speed[idx] = 0.2f + ((rand() % 300) / 1000.0f); Vec3 col = hsv_to_rgb(asteroid_hue[idx], 0.9f, 0.95f); asteroids[i] = (GameObject){{((rand() % 200) / 100.0f) - 1.0f, 1.1f}, {size, size}, {0.0f, -(((rand() % 10) / 100.0f) + 0.2f + (score * 0.001f))}, col, 0.0f, 1}; return; } }

// IZMENJENO: processInput sada ima i taster 'R' za restart
void processInput(GLFWwindow *window, double dt) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, 1);
    
    static int m_key_was_pressed = 0;
    int m_key_is_pressed = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
    if (m_key_is_pressed && !m_key_was_pressed) {
        missed_asteroids_rule_enabled = !missed_asteroids_rule_enabled;
        printf("Pravilo promasenih asteroida je sada: %s\n", missed_asteroids_rule_enabled ? "UKLJUCENO" : "ISKLJUCENO");
        fflush(stdout);
    }
    m_key_was_pressed = m_key_is_pressed;

    // NOVO: Logika za restart
    if (game_over) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            initialize_game();
        }
        return;
    }

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) player.position.x -= player.velocity.x * dt;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) player.position.x += player.velocity.x * dt;

    if (player.position.x > 1.0f) player.position.x = 1.0f;
    if (player.position.x < -1.0f) player.position.x = -1.0f;
    
    shoot_cooldown -= dt;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && shoot_cooldown <= 0.0) {
        shoot_bullet();
        shoot_cooldown = 0.25;
    }
}

// --- Funkcije za crtanje ---
void draw_rect(float x, float y, float w, float h, int trans_loc, int scale_loc, int rot_loc, int color_loc, Vec3 c) {
    glUniform2f(trans_loc, x, y); glUniform2f(scale_loc, w, h);
    glUniform1f(rot_loc, 0.0f); glUniform3f(color_loc, c.r, c.g, c.b);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}
void draw_digit(int digit, float x, float y, float size, int trans_loc, int scale_loc, int rot_loc, int color_loc, Vec3 c) {
    // Novi prikaz cifara: jednostavan 3x5 "pixel" font umesto 7-segmentnog
    // Razlog: 7-segmentna verzija je imala nelogicne duzine segmenata i
    // preklapanja pa su se cifre iskrivljavale (videlo se kao "IAA").
    if (digit < 0 || digit > 9) return;

    static const char *patterns[10] = {
        "111""101""101""101""111", // 0
        "010""010""010""010""010", // 1
        "111""001""111""100""111", // 2
        "111""001""111""001""111", // 3
        "101""101""111""001""001", // 4
        "111""100""111""001""111", // 5
        "111""100""111""101""111", // 6
        "111""001""001""001""001", // 7
        "111""101""111""101""111", // 8
        "111""101""111""001""111"  // 9
    };

    // Zgusnuti raspored "piksela" da linije ne izgledaju kao tackice
    const float STEP_FACTOR = 1.35f;     // ranije 2.0f → razmak je bio prevelik
    const float PIXEL_FACTOR = 1.15f;    // blago vece kockice da se ivice spoje

    float step = size * STEP_FACTOR;     // razmak izmedju centara "piksela"
    float start_x = x - step;            // kolona 0 (levo)
    float start_y = y + step * 2.0f;     // red 0 (gore)
    float px = size * PIXEL_FACTOR;      // velicina jedne kockice

    const char *p = patterns[digit];
    for (int r = 0; r < 5; r++) {
        for (int col = 0; col < 3; col++) {
            if (p[r*3 + col] == '1') {
                float cx = start_x + col * step;
                float cy = start_y - r * step;
                draw_rect(cx, cy, px, px, trans_loc, scale_loc, rot_loc, color_loc, c);
            }
        }
    }
}
void draw_score(int score_val, float x, float y, float size, int trans_loc, int scale_loc, int rot_loc, int color_loc, Vec3 c) {
    if (score_val == 0) { draw_digit(0, x, y, size, trans_loc, scale_loc, rot_loc, color_loc, c); return; }
    char buffer[16]; sprintf(buffer, "%d", score_val);
    int num_digits = (int)strlen(buffer);
    // Blago uvecan razmak izmedju cifara radi citkosti
    float advance = size * 6.6f;            // bilo 6.0f
    float start_x = x - (num_digits - 1) * (advance * 0.5f);
    for (int i = 0; i < num_digits; i++) {
        draw_digit(buffer[i] - '0', start_x + i * advance, y, size, trans_loc, scale_loc, rot_loc, color_loc, c);
    }
}

// ISPRAVLJENO: draw_game_over_screen sa ispravnim i jednostavnijim koordinatama
void draw_game_over_screen(int trans_loc, int scale_loc, int rot_loc, int color_loc, float anim_scale) {
    Vec3 c = {1.0f, 0.1f, 0.1f};
    float w = 0.05f * anim_scale, h = 0.05f * anim_scale;
    float y_offset = (1.0f - anim_scale) * 1.8f;
    float y_base = y_offset + 0.35f; // pomeranje natpisa ka vrhu ekrana

    // G
    draw_rect(-0.8f, 0.4f + y_base, w*3, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.9f, 0.3f + y_base, w, h*3, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.8f, 0.2f + y_base, w*3, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.7f, 0.25f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c);
    // A
    draw_rect(-0.5f, 0.4f + y_base, w*3, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.6f, 0.3f + y_base, w, h*3, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.4f, 0.3f + y_base, w, h*3, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.5f, 0.3f + y_base, w*3, h, trans_loc, scale_loc, rot_loc, color_loc, c);
    // M
    draw_rect(-0.15f, 0.3f + y_base, w, h*5, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.15f, 0.3f + y_base, w, h*5, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.075f, 0.4f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.0f, 0.3f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.075f, 0.4f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c);
    // E
    draw_rect(0.35f, 0.3f + y_base, w, h*5, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.45f, 0.4f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.45f, 0.3f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.45f, 0.2f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c);

    // O
    draw_rect(-0.6f, -0.1f + y_base, w*3, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.7f, -0.2f + y_base, w, h*3, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.5f, -0.2f + y_base, w, h*3, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.6f, -0.3f + y_base, w*3, h, trans_loc, scale_loc, rot_loc, color_loc, c);
    // V
    draw_rect(-0.3f, -0.15f + y_base, w, h*4, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.1f, -0.15f + y_base, w, h*4, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.25f, -0.3f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(-0.2f, -0.35f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c);
    // E
    draw_rect(0.1f, -0.2f + y_base, w, h*5, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.2f, 0.0f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.2f, -0.2f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.2f, -0.4f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c);
    // R
    draw_rect(0.5f, -0.2f + y_base, w, h*5, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.6f, 0.0f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.7f, -0.1f + y_base, w, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.6f, -0.2f + y_base, w*2, h, trans_loc, scale_loc, rot_loc, color_loc, c); draw_rect(0.65f, -0.35f + y_base, w, h*2, trans_loc, scale_loc, rot_loc, color_loc, c);

    if (anim_scale < 1.0) return;
    
    // Trenutni skor
    draw_score(score, 0.0f, -0.3f, 0.02f, trans_loc, scale_loc, rot_loc, color_loc, (Vec3){1.0f, 1.0f, 0.5f});

    // Leaderboard: top 3 razlicite boje, ostali sivi
    for (int i = 0; i < 4 && i < leaderboard_count; i++) {
        Vec3 lc;
        if (i == 0) lc = (Vec3){1.0f, 0.84f, 0.0f};       // zlato
        else if (i == 1) lc = (Vec3){0.75f, 0.75f, 0.75f}; // srebro
        else if (i == 2) lc = (Vec3){0.8f, 0.5f, 0.2f};    // bronza
        else lc = (Vec3){0.5f, 0.5f, 0.5f};                // sivi
        draw_score(leaderboard[i].score, 0.0f, -0.5f - i * 0.12f, 0.015f, trans_loc, scale_loc, rot_loc, color_loc, lc);
    }
}

// --- Glavna logika igre ---
void update_state(GLFWwindow* window, double dt) {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].position.y -= stars[i].speed * dt;
        if (stars[i].position.y < -1.1f) {
            stars[i].position.y = 1.1f;
            stars[i].position.x = ((rand() % 2000) / 1000.0f) - 1.0f;
        }
    }
    if (game_over) return;
    for (int i = 0; i < MAX_BULLETS; i++) if (bullets[i].active) {
        bullets[i].position.y += bullets[i].velocity.y * dt;
        if (bullets[i].position.y > 1.1f) bullets[i].active = 0;
    }
    double spawn_interval = 1.0 - (score * 0.002);
    if (spawn_interval < 0.2) spawn_interval = 0.2;
    asteroid_spawn_timer += dt;
    if (asteroid_spawn_timer > spawn_interval) {
        spawn_asteroid();
        asteroid_spawn_timer = 0.0;
    }
    for (int i = 0; i < MAX_ASTEROIDS; i++) if (asteroids[i].active) {
        asteroids[i].position.y += asteroids[i].velocity.y * dt;
        asteroids[i].rotation += 1.0f * dt;
        // Ažuriraj nijansu za "vibriranje" boje dok asteroid pada
        asteroid_hue[i] += asteroid_hue_speed[i] * (float)dt;
        if (asteroid_hue[i] >= 1.0f) asteroid_hue[i] -= 1.0f;
        if (asteroid_hue[i] < 0.0f) asteroid_hue[i] += 1.0f;
        asteroids[i].color = hsv_to_rgb(asteroid_hue[i], 0.9f, 0.95f);
        if (asteroids[i].position.y < -1.2f) { asteroids[i].active = 0; asteroids_missed++; }
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        for (int j = 0; j < MAX_ASTEROIDS; j++) {
            if (!asteroids[j].active) continue;
            float r1 = bullets[i].size.y / 2.0f, r2 = asteroids[j].size.x / 2.0f;
            float dx = bullets[i].position.x - asteroids[j].position.x, dy = bullets[i].position.y - asteroids[j].position.y;
            if ((dx * dx + dy * dy) < (r1 + r2)*(r1 + r2)) {
                bullets[i].active = 0; asteroids[j].active = 0; score += 10; break; 
            }
        }
    }

    int should_be_game_over = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!asteroids[i].active) continue;
        float r1 = player.size.x / 2.5f, r2 = asteroids[i].size.x / 2.0f;
        float dx = player.position.x - asteroids[i].position.x, dy = player.position.y - asteroids[i].position.y;
        if ((dx * dx + dy * dy) < (r1 + r2)*(r1+r2)) { should_be_game_over = 1; break; }
    }
    if (missed_asteroids_rule_enabled && asteroids_missed >= MISSED_ASTEROID_LIMIT) {
        should_be_game_over = 1;
    }
    if (should_be_game_over && !game_over) {
        game_over = 1;
        player.color.r = 1.0f; player.color.g = 0.2f; player.color.b = 0.2f;
        add_score_to_leaderboard(score);
    }

    char title[200];
    if (game_over) {
         sprintf(title, "KRAJ IGRE! | Konacan rezultat: %d | Pritisni 'R' za ponovo", score);
    } else {
        sprintf(title, "Svemirski Begunac | Rezultat: %d | Promaseno: %d/%d | Pravilo [M]: %s", 
                score, asteroids_missed, MISSED_ASTEROID_LIMIT, missed_asteroids_rule_enabled ? "ON" : "OFF");
    }
    glfwSetWindowTitle(window, title);
}

// --- MAIN funkcija ---
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(800, 900, "Svemirski Begunac", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    float quad_vertices[] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    unsigned int quad_indices[] = {0, 1, 2, 2, 3, 0};
    unsigned int quadVAO, quadVBO, quadEBO;
    glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO); glGenBuffers(1, &quadEBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_indices), quad_indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    
    float player_vertices[] = {0.0f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};
    unsigned int playerVAO, playerVBO;
    glGenVertexArrays(1, &playerVAO); glGenBuffers(1, &playerVBO);
    glBindVertexArray(playerVAO);
    glBindBuffer(GL_ARRAY_BUFFER, playerVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(player_vertices), player_vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);

    srand(time(NULL));
    load_leaderboard();
    atexit(print_full_leaderboard);
    initialize_game();
    double lastFrame = 0.0;

    while (!glfwWindowShouldClose(window)) {
        double currentFrame = glfwGetTime();
        double deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        processInput(window, deltaTime);
        update_state(window, deltaTime);
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);
        int translateLoc = glGetUniformLocation(shaderProgram, "u_Translate");
        int scaleLoc = glGetUniformLocation(shaderProgram, "u_Scale");
        int rotationLoc = glGetUniformLocation(shaderProgram, "u_Rotation");
        int colorLoc = glGetUniformLocation(shaderProgram, "u_Color");
        
        glBindVertexArray(quadVAO);
        for (int i = 0; i < MAX_STARS; i++) {
            float size, brightness;
            if (stars[i].layer == 0) { size = 0.005f; brightness = 0.3f; }
            else if (stars[i].layer == 1) { size = 0.008f; brightness = 0.6f; }
            else { size = 0.012f; brightness = 1.0f; }
            glUniform2f(translateLoc, stars[i].position.x, stars[i].position.y);
            glUniform2f(scaleLoc, size, size);
            glUniform1f(rotationLoc, 0.0f);
            glUniform3f(colorLoc, brightness, brightness, brightness);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }
        
        if (!game_over) {
            for (int i = 0; i < MAX_ASTEROIDS; i++) if (asteroids[i].active) {
                glUniform2f(translateLoc, asteroids[i].position.x, asteroids[i].position.y);
                glUniform2f(scaleLoc, asteroids[i].size.x, asteroids[i].size.y);
                glUniform1f(rotationLoc, asteroids[i].rotation);
                glUniform3f(colorLoc, asteroids[i].color.r, asteroids[i].color.g, asteroids[i].color.b);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            }
            for (int i = 0; i < MAX_BULLETS; i++) if (bullets[i].active) {
                glUniform2f(translateLoc, bullets[i].position.x, bullets[i].position.y);
                glUniform2f(scaleLoc, bullets[i].size.x, bullets[i].size.y);
                glUniform1f(rotationLoc, bullets[i].rotation);
                glUniform3f(colorLoc, bullets[i].color.r, bullets[i].color.g, bullets[i].color.b);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            }
        }
        
        if (player.active) {
            glBindVertexArray(playerVAO);
            glUniform2f(translateLoc, player.position.x, player.position.y);
            glUniform2f(scaleLoc, player.size.x, player.size.y);
            glUniform1f(rotationLoc, player.rotation);
            glUniform3f(colorLoc, player.color.r, player.color.g, player.color.b);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        if (game_over) {
            game_over_animation_timer += deltaTime * 1.5;
            float anim_progress = fmin(1.0, game_over_animation_timer);
            glBindVertexArray(quadVAO);
            draw_game_over_screen(translateLoc, scaleLoc, rotationLoc, colorLoc, anim_progress);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    glDeleteVertexArrays(1, &quadVAO); glDeleteBuffers(1, &quadVBO); glDeleteBuffers(1, &quadEBO);
    glDeleteVertexArrays(1, &playerVAO); glDeleteBuffers(1, &playerVBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    
    return 0;
}