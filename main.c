#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

enum game_state { GSTATE_ENTRY, GSTATE_PLAY, GSTATE_RESULT, GSTATE_TERMINATE };

struct vec2d {
	double x;
	double y;
};

enum ui_state_entry { USTATE_ENTRY_START };

enum ui_state_result { USTATE_RESULT_CONTINUE, USTATE_RESULT_TERMINATE };

// DサフィクスはGCC固有?clangはだめかも
// const double pi = 3.14159265358979323846264338327950288D;
const double pi = 3.14159265358979323846264338327950288;
#define block_nline 3
#define block_ncolumn 6
#define hide_pos (INT_MIN / 2)

SDL_Window *window;

TTF_Font *font_ui_prompt;
TTF_Font *font_ui_select;
TTF_Font *font_ui_data;
SDL_Surface *canvas;
SDL_Surface *ball;
SDL_Surface *bar;
SDL_Surface *block;

struct game {
	SDL_Rect ball_rect;
	SDL_Rect bar_rect;
	SDL_Rect block_rects[block_nline][block_ncolumn];
	int block_rect_mark[block_nline]
	                   [block_ncolumn]; // 一旦markして、あとでまとめてsweep
	struct vec2d ball_direction;
	int n_broken_blocks;

	// for entry state
	enum ui_state_entry ui_state_entry;
	// for result state
	enum ui_state_result ui_state_result;

	// for ui
	int ui_blink_cursor;

	int ball_speed;
};

static struct game game;

// だいたい49日くらい経つとオーバーフローしてバグるといういつものやつ
unsigned int last_frame_timestamp_ms, current_timestamp_ms;

enum game_state game_state = GSTATE_ENTRY;

struct game
game_new(void)
{
	struct game g;

	g.n_broken_blocks = 0;

	// ボールの位置を初期化
	g.ball_rect = (SDL_Rect){.x = (480 / 2) - (20 / 2),
	    .y = (480 / 2) - (20 / 2),
	    .w = 20,
	    .h = 20};

	// 常に右下60度で始まる
	g.ball_direction.x = 1.0 / 2.0;
	g.ball_direction.y = sqrt(3.0) / 2.0;

	// バーの位置を初期化
	g.bar_rect = (SDL_Rect){
	    .x = (480 / 2) - (80 / 2), .y = 480 - 50, .w = 80, .h = 10};

	// ブロックの位置を初期化
	for (int line = 0; line < block_nline; line++)
		for (int column = 0; column < block_ncolumn; column++) {
			g.block_rects[line][column] = (SDL_Rect){
			    .x = 80 * column, .y = 40 * line, .w = 80, .h = 40};
			g.block_rect_mark[line][column] = 0;
		}

	g.ui_state_entry = USTATE_ENTRY_START;
	g.ui_state_result = USTATE_RESULT_CONTINUE;
	g.ui_blink_cursor = 0;
	g.ball_speed = 3;

	return g;
}

int
is_hidden(SDL_Rect r)
{
	return r.x == hide_pos && r.y == hide_pos && r.w == hide_pos &&
	    r.h == hide_pos;
}

void
hide(SDL_Rect *r)
{
	r->x = r->y = r->w = r->h = hide_pos;
}

int
clamp(int x, int min, int max)
{
	if (min <= x && x <= max)
		return x;
	else if (min > x)
		return min;
	else
		return max;
}

// 汎用の当たり判定関数を流用したので、4辺のどこに当たったのかという余計な処理がある。
void
calc_collision(SDL_Rect ball_rect_, SDL_Rect entity_rect, int *collision_up,
    int *collision_down, int *collision_left, int *collision_right)
{
	// X軸上でボールの上か下に重なった
	if ((ball_rect_.x <= (entity_rect.x + entity_rect.w)) &&
	    (entity_rect.x <= (ball_rect_.x + ball_rect_.w))) {
		// Y軸での当たり判定
		if (((entity_rect.y + entity_rect.h) >= ball_rect_.y) &&
		    ((entity_rect.y + entity_rect.h) <=
		        (ball_rect_.y + ball_rect_.h)))
			*collision_up = 1;
		// Y軸での当たり判定
		if ((entity_rect.y >= ball_rect_.y) &&
		    (entity_rect.y <= (ball_rect_.y + ball_rect_.h)))
			*collision_down = 1;
	}

	// Y軸上でボールの右か左に重なった
	if ((ball_rect_.y <= (entity_rect.y + entity_rect.h)) &&
	    (entity_rect.y <= (ball_rect_.y + ball_rect_.h))) {
		// X軸での当たり判定
		if (((entity_rect.x + entity_rect.y) >= ball_rect_.x) &&
		    ((entity_rect.x + entity_rect.y) <=
		        (ball_rect_.x + ball_rect_.w)))
			*collision_left = 1;
		// X軸での当たり判定
		if ((entity_rect.x <= (ball_rect_.x + ball_rect_.w)) &&
		    (entity_rect.x >= ball_rect_.x))
			*collision_right = 1;
	}
}

void
calc_collision_with_block(struct game *g, SDL_Rect ball_rect_,
    SDL_Rect entity_rect, int *collision_up, int *collision_down,
    int *collision_left, int *collision_right, int line, int column)
{
	// X軸上でボールの上か下に重なった
	if ((ball_rect_.x <= (entity_rect.x + entity_rect.w)) &&
	    (entity_rect.x <= (ball_rect_.x + ball_rect_.w))) {
		// Y軸での当たり判定
		if (((entity_rect.y + entity_rect.h) >= ball_rect_.y) &&
		    ((entity_rect.y + entity_rect.h) <=
		        (ball_rect_.y + ball_rect_.h)))
			if (line >= block_nline - 1 ||
			    is_hidden(g->block_rects[line + 1][column]))
				*collision_up = 1;
		// Y軸での当たり判定
		if ((entity_rect.y >= ball_rect_.y) &&
		    (entity_rect.y <= (ball_rect_.y + ball_rect_.h)))
			if (line > 0 &&
			    is_hidden(g->block_rects[line - 1][column]))
				*collision_down = 1;
	}

	// Y軸上でボールの右か左に重なった

	// 隣接するオブジェクトがあるときに横の当たり判定を消失させる
	// 処理を加えると予期しない当たり判定の消失がある気がするけど
	// 再現しないので気にしない
	if ((ball_rect_.y <= (entity_rect.y + entity_rect.h)) &&
	    (entity_rect.y <= (ball_rect_.y + ball_rect_.h))) {
		// X軸での当たり判定
		if (((entity_rect.x + entity_rect.y) >= ball_rect_.x) &&
		    ((entity_rect.x + entity_rect.y) <=
		        (ball_rect_.x + ball_rect_.w)))
			if (column < block_ncolumn - 1 &&
			    is_hidden(g->block_rects[line][column + 1]))
				*collision_left = 1;
		// X軸での当たり判定
		if ((entity_rect.x <= (ball_rect_.x + ball_rect_.w)) &&
		    (entity_rect.x >= ball_rect_.x))
			if (column > 0 &&
			    is_hidden(g->block_rects[line][column - 1]))
				*collision_right = 1;
	}
}

void
draw_world(struct game *g)
{
	// SDL_BlitSurfaceはdstrectのフィールドを上書きすることに注意
	SDL_Rect dstrect;

	// 背景
	SDL_FillRect(canvas, NULL, 0xa0a0a0);

	// ゲームと右横のUIの間の帯
	SDL_FillRect(canvas, &(SDL_Rect){480, 0, 5, 480}, 0x404040);

	// 文字列

	char *blocks_str;
	asprintf(&blocks_str, "残りブロック: %d", 18 - g->n_broken_blocks);
	struct SDL_Surface *blocks = TTF_RenderUTF8_Blended(
	    font_ui_data, blocks_str, (SDL_Color){0xff, 0xff, 0xff, 0xff});
	free(blocks_str);
	dstrect = (SDL_Rect){495, 5, 0, 0};
	SDL_BlitSurface(blocks, NULL, canvas, &dstrect);
	SDL_FreeSurface(blocks);

	struct SDL_Surface *op1 = TTF_RenderUTF8_Blended(
	    font_ui_data, "操作方法:", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){495, 65, 0, 0};
	SDL_BlitSurface(op1, NULL, canvas, &dstrect);
	SDL_FreeSurface(op1);

	struct SDL_Surface *op2 = TTF_RenderUTF8_Blended(
	    font_ui_data, "←: 右に移動", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){495, 95, 0, 0};
	SDL_BlitSurface(op2, NULL, canvas, &dstrect);
	SDL_FreeSurface(op2);

	struct SDL_Surface *op3 = TTF_RenderUTF8_Blended(
	    font_ui_data, "→: 左に移動", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){495, 125, 0, 0};
	SDL_BlitSurface(op3, NULL, canvas, &dstrect);
	SDL_FreeSurface(op3);

	struct SDL_Surface *op4 = TTF_RenderUTF8_Blended(
	    font_ui_data, "Enter: 決定", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){495, 155, 0, 0};
	SDL_BlitSurface(op4, NULL, canvas, &dstrect);
	SDL_FreeSurface(op4);

	struct SDL_Surface *op5 = TTF_RenderUTF8_Blended(
	    font_ui_data, "Q: 終了", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){495, 185, 0, 0};
	SDL_BlitSurface(op5, NULL, canvas, &dstrect);
	SDL_FreeSurface(op5);

	dstrect = (SDL_Rect){g->bar_rect.x, g->bar_rect.y, 0, 0};
	// SDL_BlitSurfaceに渡すSDL_Rectのhとwは戻り値であり、引数とし
	// ては使わない
	SDL_BlitSurface(bar, NULL, canvas, &dstrect);

	for (int line = 0; line < block_nline; line++)
		for (int column = 0; column < block_ncolumn; column++) {
			dstrect =
			    (SDL_Rect){.x = g->block_rects[line][column].x,
			        .y = g->block_rects[line][column].y,
			        .w = 0,
			        .h = 0};
			SDL_BlitSurface(block, NULL, canvas, &dstrect);
		}

	dstrect = (SDL_Rect){g->ball_rect.x, g->ball_rect.y, 0, 0};
	SDL_BlitSurface(ball, NULL, canvas, &dstrect);
}

void
game_play_mainloop(struct game *g)
{
	int last_n_broken_blocs = g->n_broken_blocks;

	// 物理(?)演算

	int collision_up = 0;
	int collision_down = 0;
	int collision_left = 0;
	int collision_right = 0;
	// int collision_bar = 0;
	int ruin_ball = 0;

	// 壁判定
	if (g->ball_rect.x <= 0)
		collision_left = 1;
	if (g->ball_rect.x >= (480 - 20))
		collision_right = 1;
	if (g->ball_rect.y <= 0)
		collision_up = 1;
	if (g->ball_rect.y >= (480 + 20))
		ruin_ball = 1;

	// バー判定
	calc_collision(g->ball_rect, g->bar_rect, &collision_up,
	    &collision_down, &collision_left, &collision_right);

	// ブロック当たり判定
	for (int line = 0; line < block_nline; line++)
		for (int column = 0; column < block_ncolumn; column++) {
			int collision_block_up = 0, collision_block_down = 0,
			    collision_block_left = 0, collision_block_right = 0;
			calc_collision_with_block(g, g->ball_rect,
			    g->block_rects[line][column], &collision_block_up,
			    &collision_block_down, &collision_block_left,
			    &collision_block_right, line, column);
			collision_up |= collision_block_up;
			collision_down |= collision_block_down;
			collision_left |= collision_block_left;
			collision_right |= collision_block_right;
			int nblockcollision = collision_block_up +
			    collision_block_down + collision_block_left +
			    collision_block_right;
			if (nblockcollision > 0) {
				g->n_broken_blocks++;
				// 一部の条件下で
				// calc_collision_with_blockは隣接ブロッ
				// クの有無を当たり判定に使うので、
				// hideはあと。
				// calc_collision_with_blockがなかった
				// としても、この手のはmark and sweep
				// にするべき。
				// hide(&g->block_rects[line][column]);
				g->block_rect_mark[line][column] = 1;
			}
		}

	int ncollision =
	    collision_left + collision_right + collision_up + collision_down;
	if (ncollision == 1) {
		if (collision_left || collision_right)
			g->ball_direction.x *= -1;
		if (collision_up || collision_down)
			g->ball_direction.y *= -1;
	} else if (ncollision > 1) {
		g->ball_direction.x *= -1;
		g->ball_direction.y *= -1;
	}

	g->ball_rect.x += (int)(g->ball_direction.x * g->ball_speed);
	g->ball_rect.y += (int)(g->ball_direction.y * g->ball_speed);

	for (int line = 0; line < block_nline; line++)
		for (int column = 0; column < block_ncolumn; column++)
			if (g->block_rect_mark[line][column])
				hide(&g->block_rects[line][column]);

	// 描画
	draw_world(g);
	SDL_UpdateWindowSurface(window);

	// 入力処理

	// https://stackoverflow.com/questions/29373203/sdl-2-0-key-repeat-and-delay/29382114#29382114
	const Uint8 *keyst = SDL_GetKeyboardState(NULL);
	if (keyst[SDL_SCANCODE_RIGHT]) {
		g->bar_rect.x = clamp(g->bar_rect.x + 5, 0, 480 - 80);
	} else if (keyst[SDL_SCANCODE_LEFT]) {
		g->bar_rect.x = clamp(g->bar_rect.x - 5, 0, 480 - 80);
	}

	if (last_n_broken_blocs < 1 && g->n_broken_blocks >= 1)
		g->ball_speed += 1;
	if (last_n_broken_blocs < 5 && g->n_broken_blocks >= 5)
		g->ball_speed += 2;
	if (last_n_broken_blocs < 12 && g->n_broken_blocks >= 12)
		g->ball_speed += 1;

	// 状態遷移
	if (g->n_broken_blocks >= 18 || ruin_ball)
		game_state = GSTATE_RESULT;
}

void
game_entry_mainloop(struct game *g)
{
	// 描画
	draw_world(g);
	SDL_Rect dstrect;

	SDL_Surface *mask;
	mask = SDL_CreateRGBSurfaceWithFormat(
	    0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
	SDL_FillRect(mask, NULL, 0x80202020);
	dstrect = (SDL_Rect){0, 0, 0, 0};
	SDL_BlitSurface(mask, NULL, canvas, &dstrect);

	SDL_Surface *title = TTF_RenderUTF8_Blended(font_ui_prompt,
	    "ブロック崩し", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){160, 100, 0, 0};
	SDL_BlitSurface(title, NULL, canvas, &dstrect);

	SDL_Surface *start = TTF_RenderUTF8_Blended(
	    font_ui_select, "START", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){260, 240, 0, 0};
	SDL_BlitSurface(start, NULL, canvas, &dstrect);

	g->ui_blink_cursor++;
	g->ui_blink_cursor %= 30; // fpsの半分

	SDL_Surface *cursor_start_left = NULL;
	SDL_Surface *cursor_start_right = NULL;
	switch (g->ui_state_entry) {
	case USTATE_ENTRY_START: {
		if (g->ui_blink_cursor < 15) {
			cursor_start_left =
			    TTF_RenderUTF8_Blended(font_ui_select, ">",
			        (SDL_Color){0xff, 0xff, 0xff, 0xff});
			dstrect = (SDL_Rect){
			    260 - cursor_start_left->w - 5, 240, 0, 0};
			SDL_BlitSurface(
			    cursor_start_left, NULL, canvas, &dstrect);
			cursor_start_right =
			    TTF_RenderUTF8_Blended(font_ui_select, "<",
			        (SDL_Color){0xff, 0xff, 0xff, 0xff});
			dstrect = (SDL_Rect){260 + start->w + 5, 240, 0, 0};
			SDL_BlitSurface(
			    cursor_start_right, NULL, canvas, &dstrect);
		}
		break;
	}
	}

	SDL_UpdateWindowSurface(window);

	// 入力 SDL_PoolEvent使うと何故か固まった
	const Uint8 *keyst = SDL_GetKeyboardState(NULL);
	if (keyst[SDL_SCANCODE_RETURN]) {
		// 状態遷移
		game_state = GSTATE_PLAY;
	}

	// cleanup
	SDL_FreeSurface(mask);
	SDL_FreeSurface(title);
	SDL_FreeSurface(start);
	SDL_FreeSurface(cursor_start_left);
	SDL_FreeSurface(cursor_start_right);
}

void
game_result_mainloop(struct game *g)
{
	// 描画
	draw_world(g);
	SDL_Rect dstrect;

	SDL_Surface *mask;
	mask = SDL_CreateRGBSurfaceWithFormat(
	    0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
	SDL_FillRect(mask, NULL, 0x80202020);
	dstrect = (SDL_Rect){0, 0, 0, 0};
	SDL_BlitSurface(mask, NULL, canvas, &dstrect);

	SDL_Surface *title = NULL;
	if (g->n_broken_blocks >= 18) {
		title = TTF_RenderUTF8_Blended(font_ui_prompt, "GAME CLEAR!",
		    (SDL_Color){0xff, 0xff, 0xff, 0xff});
		dstrect = (SDL_Rect){165, 100, 0, 0};
		SDL_BlitSurface(title, NULL, canvas, &dstrect);
	} else {
		title = TTF_RenderUTF8_Blended(font_ui_prompt, "GAME OVER",
		    (SDL_Color){0xff, 0xff, 0xff, 0xff});
		dstrect = (SDL_Rect){180, 100, 0, 0};
		SDL_BlitSurface(title, NULL, canvas, &dstrect);
	}

	SDL_Surface *continue_ = TTF_RenderUTF8_Blended(
	    font_ui_select, "CONTINUE", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){170, 240, 0, 0};
	SDL_BlitSurface(continue_, NULL, canvas, &dstrect);

	SDL_Surface *terminate = TTF_RenderUTF8_Blended(
	    font_ui_select, "TERMINATE", (SDL_Color){0xff, 0xff, 0xff, 0xff});
	dstrect = (SDL_Rect){330, 240, 0, 0};
	SDL_BlitSurface(terminate, NULL, canvas, &dstrect);

	g->ui_blink_cursor++;
	g->ui_blink_cursor %= 30; // fpsの半分

	SDL_Surface *cursor_continue_left = NULL;
	SDL_Surface *cursor_continue_right = NULL;
	SDL_Surface *cursor_terminate_left = NULL;
	SDL_Surface *cursor_terminate_right = NULL;
	switch (g->ui_state_result) {
	case USTATE_RESULT_CONTINUE: {
		if (g->ui_blink_cursor < 15) {
			cursor_continue_left =
			    TTF_RenderUTF8_Blended(font_ui_select, ">",
			        (SDL_Color){0xff, 0xff, 0xff, 0xff});
			dstrect = (SDL_Rect){
			    170 - cursor_continue_left->w - 5, 240, 0, 0};
			SDL_BlitSurface(
			    cursor_continue_left, NULL, canvas, &dstrect);
			cursor_continue_right =
			    TTF_RenderUTF8_Blended(font_ui_select, "<",
			        (SDL_Color){0xff, 0xff, 0xff, 0xff});
			dstrect = (SDL_Rect){170 + continue_->w + 5, 240, 0, 0};
			SDL_BlitSurface(
			    cursor_continue_right, NULL, canvas, &dstrect);
		}
		break;
	}
	case USTATE_RESULT_TERMINATE: {
		if (g->ui_blink_cursor < 15) {
			cursor_terminate_left =
			    TTF_RenderUTF8_Blended(font_ui_select, ">",
			        (SDL_Color){0xff, 0xff, 0xff, 0xff});
			dstrect = (SDL_Rect){
			    330 - cursor_terminate_left->w - 5, 240, 0, 0};
			SDL_BlitSurface(
			    cursor_terminate_left, NULL, canvas, &dstrect);
			cursor_terminate_right =
			    TTF_RenderUTF8_Blended(font_ui_select, "<",
			        (SDL_Color){0xff, 0xff, 0xff, 0xff});
			dstrect = (SDL_Rect){330 + terminate->w + 5, 240, 0, 0};
			SDL_BlitSurface(
			    cursor_terminate_right, NULL, canvas, &dstrect);
		}
		break;
	}
	}

	SDL_UpdateWindowSurface(window);

	// 入力 SDL_PoolEvent使うと何故か固まった
	const Uint8 *keyst = SDL_GetKeyboardState(NULL);

	switch (g->ui_state_result) {
	case USTATE_RESULT_CONTINUE: {
		if (keyst[SDL_SCANCODE_RETURN]) {
			// 状態遷移
			game = game_new();
			game_state = GSTATE_PLAY;
		} else if (keyst[SDL_SCANCODE_RIGHT]) {
			g->ui_state_result = USTATE_RESULT_TERMINATE;
		}
		break;
	}
	case USTATE_RESULT_TERMINATE: {
		if (keyst[SDL_SCANCODE_RETURN]) {
			// 状態遷移
			game_state = GSTATE_TERMINATE;
		} else if (keyst[SDL_SCANCODE_LEFT]) {
			g->ui_state_result = USTATE_RESULT_CONTINUE;
		}
		break;
	}
	}

	// cleanup
	SDL_FreeSurface(mask);
	SDL_FreeSurface(title);
	SDL_FreeSurface(continue_);
	SDL_FreeSurface(terminate);
	SDL_FreeSurface(cursor_continue_left);
	SDL_FreeSurface(cursor_continue_right);
	SDL_FreeSurface(cursor_terminate_left);
	SDL_FreeSurface(cursor_terminate_right);
}

void
game_mainloop(void *arg)
{
	struct game *g = arg;

	// フレームレートの管理

	current_timestamp_ms = SDL_GetTicks();
	// 1フレーム16msで、だいたい62.5FPSになる
	if (current_timestamp_ms - last_frame_timestamp_ms < 16)
		return;
	last_frame_timestamp_ms = current_timestamp_ms;

	// 状態による分岐
	switch (game_state) {
	case GSTATE_ENTRY:
		game_entry_mainloop(g);
		break;
	case GSTATE_PLAY:
		game_play_mainloop(g);
		break;
	case GSTATE_RESULT:
		game_result_mainloop(g);
		break;
	case GSTATE_TERMINATE:
		goto cleanup;
	}

	// どこでも有効な入力処理
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			goto cleanup;
		} else if (event.type == SDL_KEYUP &&
		    event.key.keysym.sym == SDLK_q) {
			goto cleanup;
		}
	}

	return;

cleanup:
	TTF_CloseFont(font_ui_prompt);
	TTF_CloseFont(font_ui_select);
	TTF_CloseFont(font_ui_data);
	SDL_FreeSurface(canvas);
	SDL_FreeSurface(ball);
	SDL_FreeSurface(bar);
	SDL_FreeSurface(block);

	TTF_Quit();
	SDL_DestroyWindow(window);
	SDL_Quit();
	printf("おわり\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();
	printf("(c) 2021 Itoh Shimon\n");
	printf("詳細は https://postfixnotation.org/misc/ を参照のこと。\n");
	printf("ゲームが始まる\n");

	window = SDL_CreateWindow("ブロック崩し", SDL_WINDOWPOS_UNDEFINED,
	    SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_OPENGL);
	if (window == NULL) {
		printf("error: SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}

	canvas = SDL_GetWindowSurface(window);

	font_ui_prompt = TTF_OpenFont("NotoSansCJKjp-Regular.otf", 48);
	font_ui_select = TTF_OpenFont("NotoSansCJKjp-Regular.otf", 24);
	font_ui_data = TTF_OpenFont("NotoSansCJKjp-Regular.otf", 18);

	if ((ball = IMG_Load("ball.png")) == NULL) {
		printf("error: IMG_Load(\"ball.png\"): %s\n", SDL_GetError());
		return 1;
	}

	if ((bar = IMG_Load("bar.png")) == NULL) {
		printf("error: IMG_Load(\"bar.png\"): %s\n", SDL_GetError());
		return 1;
	}

	if ((block = IMG_Load("block.png")) == NULL) {
		printf("error: IMG_Load(\"block.png\"): %s\n", SDL_GetError());
		return 1;
	}

	last_frame_timestamp_ms = SDL_GetTicks();

	game = game_new();

#ifdef __EMSCRIPTEN__
	// https://github.com/emscripten-ports/SDL2/issues/128
	// https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_set_main_loop
	emscripten_set_main_loop_arg(game_mainloop, &game, 0, 1);
#else
	for (;;)
		game_mainloop(&game);
#endif

	__builtin_unreachable();
}
