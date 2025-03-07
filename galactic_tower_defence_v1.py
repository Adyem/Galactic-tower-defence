import pygame
import sys
import math
import random

# --- Global Settings and Colors ---
WIDTH, HEIGHT = 800, 600
WHITE  = (255, 255, 255)
BLACK  = (0, 0, 0)
RED    = (255, 0, 0)
GREEN  = (0, 255, 0)
BLUE   = (0, 0, 255)
GRAY   = (100, 100, 100)
YELLOW = (255, 255, 0)

# --- Button Class ---
class Button:
    def __init__(self, x, y, width, height, text, action, font, bg_color=GRAY, text_color=WHITE):
        self.rect = pygame.Rect(x, y, width, height)
        self.text = text
        self.action = action
        self.font = font
        self.bg_color = bg_color
        self.text_color = text_color
    
    def draw(self, surface):
        pygame.draw.rect(surface, self.bg_color, self.rect)
        text_surf = self.font.render(self.text, True, self.text_color)
        surface.blit(text_surf, (self.rect.x + (self.rect.width - text_surf.get_width()) // 2,
                                 self.rect.y + (self.rect.height - text_surf.get_height()) // 2))
    
    def is_clicked(self, pos):
        return self.rect.collidepoint(pos)

# --- Tower Class ---
class Tower:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.max_health = 100
        self.health = self.max_health
        self.damage = 10
        self.attack_speed = 1.0  # shots per second
        self.last_shot_time = 0
        self.range = 300
        self.gold = 100   # starting resources
        self.platinum = 5
        self.weapon_slots = []  # extra weapons added via platinum
    
    def upgrade_health(self, amount, cost):
        if self.gold >= cost:
            self.gold -= cost
            self.max_health += amount
            self.health += amount
    
    def upgrade_damage(self, amount, cost):
        if self.gold >= cost:
            self.gold -= cost
            self.damage += amount

    def add_weapon(self, weapon, cost):
        if self.platinum >= cost:
            self.platinum -= cost
            self.weapon_slots.append(weapon)
    
    def update(self, current_time, mobs):
        # Auto-shoot: if enough time has passed, target the first mob in range
        if current_time - self.last_shot_time >= 1000 / self.attack_speed:
            target = None
            for mob in mobs:
                dx = mob.x - self.x
                dy = mob.y - self.y
                if math.hypot(dx, dy) <= self.range:
                    target = mob
                    break
            if target:
                target.health -= self.damage
                self.last_shot_time = current_time
                # Fire extra weapons (each adds its own damage)
                for weapon in self.weapon_slots:
                    target.health -= weapon.damage

# --- Mob Class ---
class Mob:
    def __init__(self, x, y, health, speed, damage):
        self.x = x
        self.y = y
        self.health = health
        self.speed = speed
        self.damage = damage
        self.radius = 10
    
    def update(self, target_x, target_y):
        # Move towards the tower
        dx = target_x - self.x
        dy = target_y - self.y
        dist = math.hypot(dx, dy)
        if dist != 0:
            dx, dy = dx / dist, dy / dist
        self.x += dx * self.speed
        self.y += dy * self.speed

# --- Weapon Class ---
class Weapon:
    def __init__(self, name, damage, cost):
        self.name = name
        self.damage = damage
        self.cost = cost

# --- Main Game Class ---
class Game:
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((WIDTH, HEIGHT))
        pygame.display.set_caption("Tower Defense with Buttons")
        self.clock = pygame.time.Clock()
        self.font = pygame.font.SysFont("Arial", 24)
        self.small_font = pygame.font.SysFont("Arial", 18)
        self.tower = Tower(WIDTH // 2, HEIGHT // 2)
        self.wave = 1
        self.mobs = []
        self.game_state = "menu"  # states: menu, defense, upgrade, game_over
        self.spawn_timer = 0
        self.spawn_interval = 1000  # spawn a mob every second
        self.mobs_spawned = 0
        self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
        self.buttons = []  # current on-screen buttons
        self.setup_menu_buttons()
    
    def calculate_mobs_to_spawn(self, wave):
        # Gradually increasing mob count (slow exponential growth)
        return int(5 * (1.1 ** wave))
    
    def spawn_mob(self):
        # Spawn mob from a random edge of the screen
        side = random.choice(["left", "right", "top", "bottom"])
        if side == "left":
            x = 0
            y = random.randint(0, HEIGHT)
        elif side == "right":
            x = WIDTH
            y = random.randint(0, HEIGHT)
        elif side == "top":
            x = random.randint(0, WIDTH)
            y = 0
        else:
            x = random.randint(0, WIDTH)
            y = HEIGHT
        mob_health = 20 + self.wave * 2
        mob_speed = 1 + self.wave * 0.05
        mob_damage = 5
        self.mobs.append(Mob(x, y, mob_health, mob_speed, mob_damage))
        self.mobs_spawned += 1

    # --- Button Setup Functions ---
    def setup_menu_buttons(self):
        self.buttons = []
        btn_width, btn_height = 200, 50
        start_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2, btn_width, btn_height,
                           "Start Game", self.start_game, self.font)
        self.buttons.append(start_btn)
    
    def setup_defense_buttons(self):
        self.buttons = []
        btn_width, btn_height = 180, 40
        upgrade_health_btn = Button(WIDTH - btn_width - 20, 20, btn_width, btn_height,
                                    "Upgrade Health (50 Gold)", self.upgrade_health, self.small_font)
        upgrade_damage_btn = Button(WIDTH - btn_width - 20, 70, btn_width, btn_height,
                                    "Upgrade Damage (50 Gold)", self.upgrade_damage, self.small_font)
        self.buttons.extend([upgrade_health_btn, upgrade_damage_btn])
    
    def setup_upgrade_buttons(self):
        self.buttons = []
        btn_width, btn_height = 250, 40
        buy_laser_btn = Button(50, HEIGHT // 2 - 50, btn_width, btn_height,
                               "Buy Laser (Cost 5)", self.buy_laser, self.small_font)
        buy_lightning_btn = Button(50, HEIGHT // 2 + 10, btn_width, btn_height,
                                   "Buy Lightning (Cost 8)", self.buy_lightning, self.small_font)
        self.buttons.extend([buy_laser_btn, buy_lightning_btn])
    
    def setup_game_over_buttons(self):
        self.buttons = []
        btn_width, btn_height = 200, 50
        restart_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 50, btn_width, btn_height,
                             "Restart", self.restart_game, self.font)
        self.buttons.append(restart_btn)

    # --- Button Actions ---
    def start_game(self):
        self.game_state = "defense"
        self.setup_defense_buttons()
    
    def upgrade_health(self):
        self.tower.upgrade_health(20, 50)
    
    def upgrade_damage(self):
        self.tower.upgrade_damage(5, 50)
    
    def buy_laser(self):
        laser = Weapon("Laser", 15, 5)
        self.tower.add_weapon(laser, 5)
    
    def buy_lightning(self):
        lightning = Weapon("Lightning", 25, 8)
        self.tower.add_weapon(lightning, 8)
    
    def restart_game(self):
        self.__init__()

    # --- Main Loop ---
    def run(self):
        while True:
            if self.game_state == "menu":
                self.menu_phase()
            elif self.game_state == "defense":
                self.defense_phase()
            elif self.game_state == "upgrade":
                self.upgrade_phase()
            elif self.game_state == "game_over":
                self.game_over_phase()
            self.clock.tick(60)

    # --- Menu Phase ---
    def menu_phase(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
        self.screen.fill(BLACK)
        title_text = self.font.render("Tower Defense Prototype", True, WHITE)
        self.screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, HEIGHT // 2 - 100))
        for btn in self.buttons:
            btn.draw(self.screen)
        pygame.display.flip()

    # --- Defense Phase ---
    def defense_phase(self):
        current_time = pygame.time.get_ticks()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
        self.screen.fill(BLACK)
        # Spawn mobs gradually
        if self.mobs_spawned < self.mobs_to_spawn:
            if current_time - self.spawn_timer >= self.spawn_interval:
                self.spawn_mob()
                self.spawn_timer = current_time
        
        # Update mobs and check collisions with tower
        for mob in self.mobs:
            mob.update(self.tower.x, self.tower.y)
            dx = mob.x - self.tower.x
            dy = mob.y - self.tower.y
            if math.hypot(dx, dy) < mob.radius + 20:
                self.tower.health -= mob.damage
                try:
                    self.mobs.remove(mob)
                except ValueError:
                    pass
        
        # Tower auto-shooting
        self.tower.update(current_time, self.mobs)
        
        # Check dead mobs and reward resources
        for mob in self.mobs[:]:
            if mob.health <= 0:
                self.tower.gold += 10
                self.tower.platinum += 1
                self.mobs.remove(mob)
        
        # Draw tower
        pygame.draw.circle(self.screen, BLUE, (self.tower.x, self.tower.y), 20)
        health_bar_width = 40
        health_bar_height = 5
        health_ratio = self.tower.health / self.tower.max_health
        pygame.draw.rect(self.screen, RED, (self.tower.x - health_bar_width // 2, self.tower.y - 30, health_bar_width, health_bar_height))
        pygame.draw.rect(self.screen, GREEN, (self.tower.x - health_bar_width // 2, self.tower.y - 30, int(health_bar_width * health_ratio), health_bar_height))
        
        # Draw mobs
        for mob in self.mobs:
            pygame.draw.circle(self.screen, RED, (int(mob.x), int(mob.y)), mob.radius)
        
        # Update button texts to reflect current gold
        if len(self.buttons) >= 2:
            self.buttons[0].text = f"Upgrade Health (50 Gold) - Gold: {self.tower.gold}"
            self.buttons[1].text = f"Upgrade Damage (50 Gold) - Gold: {self.tower.gold}"
        for btn in self.buttons:
            btn.draw(self.screen)
        
        # Display HUD
        hud_text = self.small_font.render(
            f"Wave: {self.wave}   Gold: {self.tower.gold}   Platinum: {self.tower.platinum}   Health: {self.tower.health}/{self.tower.max_health}",
            True, WHITE)
        self.screen.blit(hud_text, (10, 10))
        pygame.display.flip()
        
        # Check if wave is complete
        if self.mobs_spawned >= self.mobs_to_spawn and not self.mobs:
            if self.tower.health > 0:
                self.wave += 1
                self.mobs_spawned = 0
                self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
                self.game_state = "upgrade"
                self.setup_upgrade_buttons()
            else:
                self.game_state = "game_over"
        if self.tower.health <= 0:
            self.game_state = "game_over"

    # --- Upgrade Phase ---
    def upgrade_phase(self):
        # Allow 5 seconds for platinum upgrades with a countdown timer
        upgrade_duration = 5000  # milliseconds
        phase_start = pygame.time.get_ticks()
        while pygame.time.get_ticks() - phase_start < upgrade_duration:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    pygame.quit(); sys.exit()
                if event.type == pygame.MOUSEBUTTONDOWN:
                    pos = pygame.mouse.get_pos()
                    for btn in self.buttons:
                        if btn.is_clicked(pos):
                            btn.action()
            self.screen.fill(BLACK)
            # Update button texts with current platinum
            if len(self.buttons) >= 2:
                self.buttons[0].text = f"Buy Laser (Cost 5) - Platinum: {self.tower.platinum}"
                self.buttons[1].text = f"Buy Lightning (Cost 8) - Platinum: {self.tower.platinum}"
            for btn in self.buttons:
                btn.draw(self.screen)
            time_left = (upgrade_duration - (pygame.time.get_ticks() - phase_start)) // 1000
            timer_text = self.small_font.render(f"Time left: {time_left}s", True, YELLOW)
            self.screen.blit(timer_text, (WIDTH - 150, HEIGHT - 50))
            pygame.display.flip()
            self.clock.tick(60)
        self.game_state = "defense"
        self.setup_defense_buttons()

    # --- Game Over Phase ---
    def game_over_phase(self):
        self.setup_game_over_buttons()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
        self.screen.fill(BLACK)
        game_over_text = self.font.render("Game Over!", True, RED)
        self.screen.blit(game_over_text, (WIDTH // 2 - game_over_text.get_width() // 2, HEIGHT // 2 - 50))
        for btn in self.buttons:
            btn.draw(self.screen)
        pygame.display.flip()

# --- Main Execution ---
if __name__ == "__main__":
    game = Game()
    game.run()
