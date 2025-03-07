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
CYAN   = (0, 255, 255)

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

# --- Projectile Class ---
class Projectile:
    def __init__(self, start_x, start_y, target, damage, speed, effect=None):
        self.x = start_x
        self.y = start_y
        self.target = target  # target mob object
        self.damage = damage
        self.speed = speed
        self.effect = effect  # e.g. "stun" for laser projectiles
        self.active = True
    
    def update(self):
        if not self.active or not self.target:
            return
        # Move toward the target
        dx = self.target.x - self.x
        dy = self.target.y - self.y
        dist = math.hypot(dx, dy)
        if dist:
            dx, dy = dx / dist, dy / dist
        self.x += dx * self.speed
        self.y += dy * self.speed
        # If close enough, register a hit
        if math.hypot(self.target.x - self.x, self.target.y - self.y) < 10:
            self.target.health -= self.damage
            if self.effect == "stun":
                self.target.stunned_until = pygame.time.get_ticks() + 1000  # stun for 1 second
            self.active = False

# --- Addon Base Class ---
class Addon:
    def __init__(self, name, damage, cooldown):
        self.name = name
        self.damage = damage
        self.cooldown = cooldown  # in milliseconds
        self.last_fired_time = 0
        self.level = 1

    def upgrade(self):
        self.level += 1
        self.damage += 5  # increase damage per level
        # Reduce cooldown but never go below 100ms
        self.cooldown = max(100, self.cooldown - 50)

# --- Laser Addon (Stuns enemy for 1 second) ---
class LaserAddon(Addon):
    def __init__(self, damage, cooldown, range=400):
        super().__init__("Laser", damage, cooldown)
        self.range = range

    def update(self, current_time, tower, mobs):
        if current_time - self.last_fired_time >= self.cooldown:
            # Find a target within range
            for mob in mobs:
                dx = mob.x - tower.x
                dy = mob.y - tower.y
                if math.hypot(dx, dy) <= self.range:
                    mob.health -= self.damage
                    mob.stunned_until = current_time + 1000  # stun for 1 second
                    self.last_fired_time = current_time
                    # Return effect info for visual display
                    return {'type': 'laser', 'start': (tower.x, tower.y), 'end': (mob.x, mob.y)}
        return None

# --- Chain Lightning Addon (Bounces 3 times with RNG damage variation) ---
class ChainLightningAddon(Addon):
    def __init__(self, damage, cooldown, range=400):
        super().__init__("Chain Lightning", damage, cooldown)
        self.range = range

    def update(self, current_time, tower, mobs):
        if current_time - self.last_fired_time >= self.cooldown:
            # Find initial target within range
            available = [mob for mob in mobs if math.hypot(mob.x - tower.x, mob.y - tower.y) <= self.range]
            if not available:
                return None
            initial_target = random.choice(available)
            hit_mobs = []
            effects = []  # store each lightning bolt effect for drawing
            current_damage = self.damage
            current_target = initial_target
            for bounce in range(3):  # bounce up to 3 times
                factor = random.uniform(0.8, 1.2)
                applied_damage = current_damage * factor
                current_target.health -= applied_damage
                hit_mobs.append(current_target)
                if bounce == 0:
                    start_point = (tower.x, tower.y)
                else:
                    start_point = (prev_target.x, prev_target.y)
                end_point = (current_target.x, current_target.y)
                effects.append({'start': start_point, 'end': end_point, 'damage': applied_damage})
                prev_target = current_target
                # Look for a new target (not already hit) within range of the current target
                candidates = [mob for mob in mobs if mob not in hit_mobs and math.hypot(mob.x - current_target.x, mob.y - current_target.y) <= self.range]
                if not candidates:
                    break
                current_target = random.choice(candidates)
                current_damage *= 0.7  # reduce damage for next bounce
            self.last_fired_time = current_time
            return {'type': 'chain', 'effects': effects}
        return None

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
        # For addons: purchased addons are bought but not yet installed;
        # installed addons actively fire during defense.
        self.purchased_addons = []
        self.installed_addons = []

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

    def update(self, current_time, mobs):
        # Fire a main attack projectile if enough time has passed.
        if current_time - self.last_shot_time >= 1000 / self.attack_speed:
            target = None
            for mob in mobs:
                dx = mob.x - self.x
                dy = mob.y - self.y
                if math.hypot(dx, dy) <= self.range:
                    target = mob
                    break
            if target:
                self.last_shot_time = current_time
                # Return a new projectile (projectile speed is set to 10)
                return Projectile(self.x, self.y, target, self.damage, speed=10)
        return None

# --- Mob Class ---
class Mob:
    def __init__(self, x, y, health, speed, damage):
        self.x = x
        self.y = y
        self.health = health
        self.speed = speed
        self.damage = damage
        self.radius = 10
        self.stunned_until = 0  # timestamp until which mob is stunned

    def update(self, target_x, target_y, current_time):
        # If stunned, do not move.
        if current_time < self.stunned_until:
            return
        dx = target_x - self.x
        dy = target_y - self.y
        dist = math.hypot(dx, dy)
        if dist:
            dx, dy = dx / dist, dy / dist
        self.x += dx * self.speed
        self.y += dy * self.speed

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
        self.projectiles = []  # list for main attack projectiles
        self.addon_effects = []  # temporary visual effects from addons (laser/chain)
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
        return 5 + wave * 2
    
    def spawn_mob(self):
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
        mob_health = 10 + self.wave * 5
        mob_speed = 1 + self.wave * 0.05
        mob_damage = 2 + self.wave * 2
        self.mobs.append(Mob(x, y, mob_health, mob_speed, mob_damage))
        self.mobs_spawned += 1

    # --- Button Setup Functions ---
    def setup_menu_buttons(self):
        self.buttons = []
        btn_width, btn_height = 500, 50
        start_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 - 60, btn_width, btn_height,
                           "Start Game", self.start_game, self.font)
        perm_upg_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 10, btn_width, btn_height,
                              "Permanent Upgrades", self.permanent_upgrades, self.font)
        quit_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 80, btn_width, btn_height,
                          "Quit", self.quit_game, self.font)
        self.buttons.extend([start_btn, perm_upg_btn, quit_btn])
    
    def setup_defense_buttons(self):
        self.buttons = []
        btn_width, btn_height = 440, 40
        upgrade_health_btn = Button(WIDTH - btn_width - 20, 20, btn_width, btn_height,
                                    f"Upgrade Health (Cost: {self.health_upgrade_cost})", self.upgrade_health, self.small_font)
        upgrade_damage_btn = Button(WIDTH - btn_width - 20, 70, btn_width, btn_height,
                                    f"Upgrade Damage (Cost: {self.damage_upgrade_cost})", self.upgrade_damage, self.small_font)
        upgrade_speed_btn = Button(WIDTH - btn_width - 20, 120, btn_width, btn_height,
                                   f"Upgrade Attack Speed (Cost: {self.speed_upgrade_cost})", self.upgrade_attack_speed, self.small_font)
        self.buttons.extend([upgrade_health_btn, upgrade_damage_btn, upgrade_speed_btn])
    
    def update_permanent_buttons(self):
        # Build dynamic buttons for the permanent upgrade screen.
        self.buttons = []
        btn_width, btn_height = 300, 50
        # Buttons to buy addons:
        buy_laser_btn = Button(50, 100, btn_width, btn_height,
                               "Buy Laser (Cost: 5)", self.buy_laser, self.small_font)
        buy_chain_btn = Button(400, 100, btn_width, btn_height,
                               "Buy Chain Lightning (Cost: 10)", self.buy_chain_lightning, self.small_font)
        self.buttons.extend([buy_laser_btn, buy_chain_btn])
        
        # Dynamic buttons for available (purchased but not installed) addons:
        y_offset = 200
        for i, addon in enumerate(self.tower.purchased_addons):
            btn = Button(50, y_offset + i*60, btn_width, btn_height,
                         f"Install {addon.name}", lambda a=addon: self.install_addon(a), self.small_font)
            self.buttons.append(btn)
        # Dynamic buttons for installed addons (upgrade them):
        y_offset = 200
        for i, addon in enumerate(self.tower.installed_addons):
            btn = Button(400, y_offset + i*60, btn_width, btn_height,
                         f"Upgrade {addon.name} (Lv {addon.level})", lambda a=addon: self.upgrade_addon(a), self.small_font)
            self.buttons.append(btn)
        
        # Back button:
        back_btn = Button(WIDTH//2 - 150, HEIGHT - 100, 300, 50, "Back", self.back_to_menu, self.small_font)
        self.buttons.append(back_btn)
    
    # --- Button Actions ---
    def start_game(self):
        self.game_state = "defense"
        self.setup_defense_buttons()
        # Reset wave and mob counters for a new defense session.
        self.wave = 1
        self.mobs = []
        self.projectiles = []
        self.addon_effects = []
        self.spawn_timer = pygame.time.get_ticks()
        self.mobs_spawned = 0
        self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
    
    def permanent_upgrades(self):
        self.game_state = "permanent_upgrades"
        self.update_permanent_buttons()
    
    def back_to_menu(self):
        # Reset game-specific states.
        self.buttons = []
        self.game_state = "menu"
        self.wave = 1
        self.mobs = []
        self.projectiles = []
        self.addon_effects = []
        self.mobs_spawned = 0
        self.mobs_to_spawn = self.calculate_mobs_to_spawn(self.wave)
        self.health_upgrade_cost = 50
        self.damage_upgrade_cost = 50
        self.speed_upgrade_cost = 50
        self.setup_menu_buttons()

    def quit_game(self):
        pygame.quit()
        sys.exit()
    
    def upgrade_health(self):
        if self.tower.gold >= self.health_upgrade_cost:
            self.tower.upgrade_health(20, self.health_upgrade_cost)
            self.health_upgrade_cost += 10
    
    def upgrade_damage(self):
        if self.tower.gold >= self.damage_upgrade_cost:
            self.tower.upgrade_damage(5, self.damage_upgrade_cost)
            self.damage_upgrade_cost += 10
    
    def upgrade_attack_speed(self):
        if self.tower.gold >= self.speed_upgrade_cost:
            self.tower.upgrade_attack_speed(0.5, self.speed_upgrade_cost)
            self.speed_upgrade_cost += 10

    def buy_laser(self):
        # Prevent duplicate purchase
        if any(a.name == "Laser" for a in self.tower.purchased_addons + self.tower.installed_addons):
            return
        if self.tower.platinum >= 5:
            self.tower.platinum -= 5
            addon = LaserAddon(15, 2000)
            self.tower.purchased_addons.append(addon)
    
    def buy_chain_lightning(self):
        if any(a.name == "Chain Lightning" for a in self.tower.purchased_addons + self.tower.installed_addons):
            return
        if self.tower.platinum >= 10:
            self.tower.platinum -= 10
            addon = ChainLightningAddon(25, 3000)
            self.tower.purchased_addons.append(addon)
    
    def install_addon(self, addon):
        if len(self.tower.installed_addons) < 4:
            self.tower.installed_addons.append(addon)
            self.tower.purchased_addons.remove(addon)
    
    def upgrade_addon(self, addon):
        addon.upgrade()
    
    def restart_game(self):
        self.__init__()
    
    # --- Pause Menu During Defense ---
    def pause_menu(self):
        btn_width, btn_height = 400, 50
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
                # Update dynamic buttons each click
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
                        self.update_permanent_buttons()
        self.screen.fill(BLACK)
        title_text = self.font.render("Permanent Upgrades", True, WHITE)
        self.screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, 30))
        res_text = self.small_font.render(f"Platinum: {self.tower.platinum}    Gold: {self.tower.gold}", True, YELLOW)
        self.screen.blit(res_text, (20, 70))
        # Display lists of available and installed addons
        avail_text = self.small_font.render("Purchased Addons (Not Installed):", True, WHITE)
        self.screen.blit(avail_text, (50, 70))
        installed_text = self.small_font.render("Installed Addons:", True, WHITE)
        self.screen.blit(installed_text, (400, 70))
        for btn in self.buttons:
            btn.draw(self.screen)
        pygame.display.flip()
    
    # --- Defense Phase ---
    def defense_phase(self):
        if self.game_state != "defense":
            return
        current_time = pygame.time.get_ticks()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    self.pause_menu()
                    if self.game_state != "defense":
                        return
            if event.type == pygame.MOUSEBUTTONDOWN:
                pos = pygame.mouse.get_pos()
                for btn in self.buttons:
                    if btn.is_clicked(pos):
                        btn.action()
        
        self.screen.fill(BLACK)
        
        # Spawn mobs gradually.
        if self.mobs_spawned < self.mobs_to_spawn:
            if current_time - self.spawn_timer >= self.spawn_interval:
                self.spawn_mob()
                self.spawn_timer = current_time
        
        # Update mobs.
        for mob in self.mobs:
            mob.update(self.tower.x, self.tower.y, current_time)
            dx = mob.x - self.tower.x
            dy = mob.y - self.tower.y
            if math.hypot(dx, dy) < mob.radius + 20:
                self.tower.health -= mob.damage
                try:
                    self.mobs.remove(mob)
                except ValueError:
                    pass
        
        # Tower main attack: fire a projectile.
        proj = self.tower.update(current_time, self.mobs)
        if proj:
            self.projectiles.append(proj)
        
        # Update projectiles.
        for proj in self.projectiles[:]:
            proj.update()
            if not proj.active:
                self.projectiles.remove(proj)
            else:
                pygame.draw.circle(self.screen, YELLOW, (int(proj.x), int(proj.y)), 5)
        
        # Update tower addons (installed modules)
        for addon in self.tower.installed_addons:
            effect = addon.update(current_time, self.tower, self.mobs)
            if effect:
                if effect['type'] == 'laser':
                    pygame.draw.line(self.screen, CYAN, effect['start'], effect['end'], 3)
                elif effect['type'] == 'chain':
                    for eff in effect['effects']:
                        pygame.draw.line(self.screen, YELLOW, eff['start'], eff['end'], 2)
        
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
        
        # Draw defense buttons (for upgrades).
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
    
    def setup_game_over_buttons(self):
        self.buttons = []
        btn_width, btn_height = 500, 50
        restart_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 50, btn_width, btn_height,
                             "Restart", self.restart_game, self.font)
        menu_btn = Button(WIDTH // 2 - btn_width // 2, HEIGHT // 2 + 120, btn_width, btn_height,
                          "Main Menu", self.back_to_menu, self.font)
        self.buttons.extend([restart_btn, menu_btn])
    
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
