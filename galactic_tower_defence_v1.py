import pygame
import sys
import math
import random

# --- Global Settings and Colors ---
WIDTH, HEIGHT = 1600, 1200  # Increased screen size (double of 800x600)
WHITE  = (255, 255, 255)
BLACK  = (0, 0, 0)
RED    = (255, 0, 0)
GREEN  = (0, 255, 0)
BLUE   = (0, 0, 255)
GRAY   = (100, 100, 100)
LIGHT_GRAY = (170, 170, 170)
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
        # Hover effect: change color when mouse is over the button
        mouse_pos = pygame.mouse.get_pos()
        color = LIGHT_GRAY if self.rect.collidepoint(mouse_pos) else self.bg_color
        pygame.draw.rect(surface, color, self.rect)
        text_surf = self.font.render(self.text, True, self.text_color)
        text_rect = text_surf.get_rect(center=self.rect.center)
        surface.blit(text_surf, text_rect)
    
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
        self.gold = 100   # starting gold
        self.platinum = 5 # starting platinum
        self.weapon_slots = []  # permanent upgrades (extra weapons)
    
    def upgrade_health(self, amount, cost):
        if self.gold >= cost:
            self.gold -= cost
            self.max_health += amount
            self.health += amount
    
    def upgrade_damage(self, amount, cost):
        if self.gold >= cost:
            self.gold -= cost
            self.damage += amount

    def upgrade_attack_speed(self, amount, cost):
        if self.gold >= cost:
            self.gold -= cost
            self.attack_speed += amount

    def add_weapon(self, weapon, cost):
        if self.platinum >= cost:
            self.platinum -= cost
            self.weapon_slots.append(weapon)
    
    def update(self, current_time, mobs, shot_effects):
        # Auto-shoot if enough time has passed.
        if current_time - self.last_shot_time >= 1000 / self.attack_speed:
            target = None
            for mob in mobs:
                dx = mob.x - self.x
                dy = mob.y - self.y
                if math.hypot(dx, dy) <= self.range:
                    target = mob
                    break
            if target:
                # Create a brief shot effect (a yellow line).
                shot_effects.append({
                    'start': (self.x, self.y),
                    'end': (target.x, target.y),
                    'start_time': current_time,
                    'duration': 100  # lasts 100 milliseconds
                })
                target.health -= self.damage
                self.last_shot_time = current_time
                # Extra permanent weapons also add their damage.
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
        # Move toward the tower.
        dx = target_x - self.x
        dy = target_y - self.y
        dist = math.hypot(dx, dy)
        if dist:
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
        pygame.display.set_caption("Tower Defense Game")
        self.clock = pygame.time.Clock()
        self.font = pygame.font.SysFont("Arial", 28)
        self.small_font = pygame.font.SysFont("Arial", 20)
        self.tower = Tower(WIDTH // 2, HEIGHT // 2)
        self.wave = 1
        self.mobs = []
        self.shot_effects = []  # for shot visual effects
        self.game_state = "menu"  # states: menu, permanent_upgrades, defense, game_over
        self.spawn_timer = 0
        self.spawn_interval = 1000  # spawn a mob every second
        self.mobs_spawned = 0
        self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
        # Upgrade costs that increase as you buy upgrades.
        self.health_upgrade_cost = 50
        self.damage_upgrade_cost = 50
        self.speed_upgrade_cost = 50
        self.buttons = []  # on-screen buttons
        self.setup_menu_buttons()
    
    def calculate_mobs_to_spawn(self, wave):
        # Enemy count increases more steeply.
        return 5 + wave * 2
    
    def spawn_mob(self):
        # Spawn mob at a random edge.
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
        # Lower starting stats with steeper scaling.
        mob_health = 10 + self.wave * 5
        mob_speed = 1 + self.wave * 0.05
        mob_damage = 2 + self.wave * 2
        self.mobs.append(Mob(x, y, mob_health, mob_speed, mob_damage))
        self.mobs_spawned += 1

    # --- Button Setup Functions ---
    def setup_menu_buttons(self):
        self.buttons = []
        btn_width, btn_height = 500, 50  # doubled button width
        start_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 - 60, btn_width, btn_height,
                           "Start Game", self.start_game, self.font)
        perm_upg_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 10, btn_width, btn_height,
                              "Permanent Upgrades", self.permanent_upgrades, self.font)
        quit_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 80, btn_width, btn_height,
                          "Quit", self.quit_game, self.font)
        self.buttons.extend([start_btn, perm_upg_btn, quit_btn])
    
    def setup_defense_buttons(self):
        self.buttons = []
        btn_width, btn_height = 440, 40  # doubled width from 220
        upgrade_health_btn = Button(WIDTH - btn_width - 20, 20, btn_width, btn_height,
                                    f"Upgrade Health (Cost: {self.health_upgrade_cost})", self.upgrade_health, self.small_font)
        upgrade_damage_btn = Button(WIDTH - btn_width - 20, 70, btn_width, btn_height,
                                    f"Upgrade Damage (Cost: {self.damage_upgrade_cost})", self.upgrade_damage, self.small_font)
        upgrade_speed_btn = Button(WIDTH - btn_width - 20, 120, btn_width, btn_height,
                                   f"Upgrade Attack Speed (Cost: {self.speed_upgrade_cost})", self.upgrade_attack_speed, self.small_font)
        self.buttons.extend([upgrade_health_btn, upgrade_damage_btn, upgrade_speed_btn])
    
    def setup_permanent_buttons(self):
        self.buttons = []
        btn_width, btn_height = 600, 50  # doubled from 300
        buy_laser_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 - 70, btn_width, btn_height,
                               "Buy Laser (Cost: 5)", self.buy_laser, self.small_font)
        buy_chain_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2, btn_width, btn_height,
                               "Buy Chain Lightning (Cost: 10)", self.buy_chain_lightning, self.small_font)
        back_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 70, btn_width, btn_height,
                          "Back", self.back_to_menu, self.small_font)
        self.buttons.extend([buy_laser_btn, buy_chain_btn, back_btn])
    
    def setup_game_over_buttons(self):
        self.buttons = []
        btn_width, btn_height = 500, 50  # doubled from 250
        restart_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 50, btn_width, btn_height,
                             "Restart", self.restart_game, self.font)
        menu_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 120, btn_width, btn_height,
                          "Main Menu", self.back_to_menu, self.font)
        self.buttons.extend([restart_btn, menu_btn])
    
    # --- Button Actions ---
    def start_game(self):
        self.game_state = "defense"
        self.setup_defense_buttons()
        # Reset wave and mob counters for a new defense session.
        self.wave = 1
        self.mobs = []
        self.shot_effects = []
        self.spawn_timer = pygame.time.get_ticks()
        self.mobs_spawned = 0
        self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
    
    def permanent_upgrades(self):
        self.game_state = "permanent_upgrades"
        self.setup_permanent_buttons()

    def back_to_menu(self):
        # Clear game-specific states that shouldn't persist in the main menu.
        self.buttons = []
        self.game_state = "menu"
        # Optionally, reinitialize any in-game variables if you want to start fresh next time:
        self.wave = 1
        self.mobs = []
        self.shot_effects = []
        self.mobs_spawned = 0
        self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
        # Also, reset upgrade costs if that makes sense:
        self.health_upgrade_cost = 50
        self.damage_upgrade_cost = 50
        self.speed_upgrade_cost = 50
        # Now set up the main menu buttons.
        self.game_state = "menu"
        self.setup_menu_buttons()

    
    def quit_game(self):
        pygame.quit()
        sys.exit()
    
    def upgrade_health(self):
        if self.tower.gold >= self.health_upgrade_cost:
            self.tower.upgrade_health(20, self.health_upgrade_cost)
            self.health_upgrade_cost += 10  # increase cost after purchase
    
    def upgrade_damage(self):
        if self.tower.gold >= self.damage_upgrade_cost:
            self.tower.upgrade_damage(5, self.damage_upgrade_cost)
            self.damage_upgrade_cost += 10
    
    def upgrade_attack_speed(self):
        if self.tower.gold >= self.speed_upgrade_cost:
            self.tower.upgrade_attack_speed(0.5, self.speed_upgrade_cost)
            self.speed_upgrade_cost += 10
    
    def buy_laser(self):
        laser = Weapon("Laser", 15, 5)
        self.tower.add_weapon(laser, 5)
    
    def buy_chain_lightning(self):
        chain = Weapon("Chain Lightning", 25, 10)
        self.tower.add_weapon(chain, 10)
    
    def restart_game(self):
        self.__init__()
    
    # --- Pause Menu During Defense ---
    def pause_menu(self):
        # Create two buttons for the pause overlay.
        btn_width, btn_height = 400, 50  # doubled from 200 width
        yes_btn = Button(WIDTH // 2 - btn_width - 10, HEIGHT // 2, btn_width, btn_height,
                         "Yes", lambda: "yes", self.font)
        no_btn = Button(WIDTH // 2 + 10, HEIGHT // 2, btn_width, btn_height,
                        "No", lambda: "no", self.font)
        pause_buttons = [yes_btn, no_btn]
        decision = None
        while decision is None:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    pygame.quit(); sys.exit()
                if event.type == pygame.MOUSEBUTTONDOWN:
                    pos = pygame.mouse.get_pos()
                    for btn in pause_buttons:
                        if btn.is_clicked(pos):
                            decision = btn.action()
            # Draw semi-transparent overlay.
            overlay = pygame.Surface((WIDTH, HEIGHT))
            overlay.set_alpha(180)
            overlay.fill(BLACK)
            self.screen.blit(overlay, (0, 0))
            pause_text = self.font.render("Return to Main Menu?", True, WHITE)
            self.screen.blit(pause_text, (WIDTH // 2 - pause_text.get_width() // 2, HEIGHT // 2 - 100))
            for btn in pause_buttons:
                btn.draw(self.screen)
            pygame.display.flip()
            self.clock.tick(60)
        # If "Yes" is chosen, return to main menu; if "No", resume defense.
        if decision == "yes":
            self.back_to_menu()
    
    # --- Main Loop ---
    def run(self):
        while True:
            if self.game_state == "menu":
                self.menu_phase()
            elif self.game_state == "permanent_upgrades":
                self.permanent_phase()
            elif self.game_state == "defense":
                self.defense_phase()
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
        title_text = self.font.render("Tower Defense Game", True, WHITE)
        self.screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, HEIGHT // 2 - 150))
        for btn in self.buttons:
            btn.draw(self.screen)
        pygame.display.flip()
    
    # --- Permanent Upgrades Phase ---
    def permanent_phase(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
        self.screen.fill(BLACK)
        title_text = self.font.render("Permanent Upgrades", True, WHITE)
        self.screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, 50))
        res_text = self.small_font.render(f"Platinum: {self.tower.platinum}", True, YELLOW)
        self.screen.blit(res_text, (20, 20))
        owned = ", ".join([w.name for w in self.tower.weapon_slots]) or "None"
        owned_text = self.small_font.render(f"Upgrades Owned: {owned}", True, WHITE)
        self.screen.blit(owned_text, (20, 50))
        for btn in self.buttons:
            btn.draw(self.screen)
        pygame.display.flip()
    
    # --- Defense Phase ---
    def defense_phase(self):
        # Immediately exit if the game state is no longer defense.
        if self.game_state != "defense":
            return

        current_time = pygame.time.get_ticks()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            # Check for pause trigger.
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    self.pause_menu()
                    # Return immediately if state has changed.
                    if self.game_state != "defense":
                        return
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
    
    # ... (rest of your defense_phase drawing and logic)

        
        self.screen.fill(BLACK)
        
        # Spawn mobs gradually.
        if self.mobs_spawned < self.mobs_to_spawn:
            if current_time - self.spawn_timer >= self.spawn_interval:
                self.spawn_mob()
                self.spawn_timer = current_time
        
        # Update mobs and detect collision with tower.
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
        
        # Tower auto-shoot and record shot effects.
        self.tower.update(current_time, self.mobs, self.shot_effects)
        
        # Draw and clear expired shot effects.
        for effect in self.shot_effects[:]:
            if current_time - effect['start_time'] > effect['duration']:
                self.shot_effects.remove(effect)
            else:
                pygame.draw.line(self.screen, YELLOW, effect['start'], effect['end'], 2)
        
        # Remove dead mobs and award resources.
        for mob in self.mobs[:]:
            if mob.health <= 0:
                self.tower.gold += 10
                self.tower.platinum += 1
                self.mobs.remove(mob)
        
        # Draw the tower with a health bar.
        pygame.draw.circle(self.screen, BLUE, (self.tower.x, self.tower.y), 20)
        health_bar_width = 40
        health_bar_height = 5
        health_ratio = self.tower.health / self.tower.max_health
        pygame.draw.rect(self.screen, RED, (self.tower.x - health_bar_width // 2, self.tower.y - 30, health_bar_width, health_bar_height))
        pygame.draw.rect(self.screen, GREEN, (self.tower.x - health_bar_width // 2, self.tower.y - 30, int(health_bar_width * health_ratio), health_bar_height))
        
        # Draw mobs.
        for mob in self.mobs:
            pygame.draw.circle(self.screen, RED, (int(mob.x), int(mob.y)), mob.radius)
        
        # Update defense buttons with the latest cost values.
        if len(self.buttons) >= 3:
            self.buttons[0].text = f"Upgrade Health (Cost: {self.health_upgrade_cost})"
            self.buttons[1].text = f"Upgrade Damage (Cost: {self.damage_upgrade_cost})"
            self.buttons[2].text = f"Upgrade Attack Speed (Cost: {self.speed_upgrade_cost})"
        for btn in self.buttons:
            btn.draw(self.screen)
        
        hud_text = self.small_font.render(
            f"Wave: {self.wave}   Gold: {self.tower.gold}   Platinum: {self.tower.platinum}   Health: {self.tower.health}/{self.tower.max_health}",
            True, WHITE)
        self.screen.blit(hud_text, (10, 10))
        pygame.display.flip()
        
        # End the wave if all mobs are spawned and cleared.
        if self.mobs_spawned >= self.mobs_to_spawn and not self.mobs:
            if self.tower.health > 0:
                self.wave += 1
                self.mobs_spawned = 0
                self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
            else:
                self.setup_game_over_buttons()
                self.game_state = "game_over"
        if self.tower.health <= 0:
            self.setup_game_over_buttons()
            self.game_state = "game_over"
    
    # --- Game Over Phase ---
    def game_over_phase(self):
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
        self.screen.blit(game_over_text, (WIDTH // 2 - game_over_text.get_width() // 2, HEIGHT // 2 - 100))
        for btn in self.buttons:
            btn.draw(self.screen)
        pygame.display.flip()

# --- Main Execution ---
if __name__ == "__main__":
    game = Game()
    game.run()
