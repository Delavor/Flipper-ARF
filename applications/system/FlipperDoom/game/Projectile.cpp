#include "game/Defines.h"
#include "game/Projectile.h"
#include "game/Map.h"
#include "game/FixedMath.h"
#include "game/Particle.h"
#include "game/Enemy.h"
#include "game/Generated/SpriteTypes.h"
#include "game/Platform.h"
#include "game/Sounds.h"

Projectile ProjectileManager::projectiles[ProjectileManager::MAX_PROJECTILES];

Projectile* ProjectileManager::FireProjectile(Entity* owner, int16_t x, int16_t y, uint8_t angle)
{
	for (Projectile& p : projectiles)
	{
		if(p.life == 0)
		{
			if (owner == &Game::player)
				p.ownerId = Projectile::playerOwnerId;
			else
			{
				for (uint8_t n = 0; n < EnemyManager::maxEnemies; n++)
				{
					if (&EnemyManager::enemies[n] == owner)
					{
						p.ownerId = n;
						break;
					}
				}
			}

			p.life = 255;
			p.x = x;
			p.y = y;
			p.angle = angle;
			return &p;
		}
	}

	return nullptr;
}

Entity* Projectile::GetOwner() const
{
	if (ownerId == playerOwnerId)
		return &Game::player;
	return &EnemyManager::enemies[ownerId];
}

void ProjectileManager::Update()
{
	for (Projectile& p : projectiles)
	{
		if(p.life > 0)
		{
			p.life--;

			int16_t deltaX = FixedCos(p.angle) / 4;
			int16_t deltaY = FixedSin(p.angle) / 4;

			p.x += deltaX;
			p.y += deltaY;

			bool hitAnything = false;

			Entity* owner = p.GetOwner();

			if (Map::IsBlockedAtWorldPosition(p.x, p.y))
			{
				uint8_t cellX = p.x / CELL_SIZE;
				uint8_t cellY = p.y / CELL_SIZE;

				if (Map::GetCellSafe(cellX, cellY) == CellType::Urn)
				{
					// Exploding barrel: splash damage to everything nearby
					int16_t barrelX = cellX * CELL_SIZE + CELL_SIZE / 2;
					int16_t barrelY = cellY * CELL_SIZE + CELL_SIZE / 2;
					constexpr int16_t blastRadius = CELL_SIZE + CELL_SIZE / 2;
					constexpr uint8_t enemyBlastDamage = 40;
					constexpr uint8_t playerBlastDamage = 15;

					Map::SetCell(cellX, cellY, CellType::Empty);
					ParticleSystemManager::CreateExplosion(barrelX, barrelY, true);

					for (uint8_t n = 0; n < EnemyManager::maxEnemies; n++)
					{
						Enemy& enemy = EnemyManager::enemies[n];
						if (!enemy.IsValid())
							continue;
						int16_t dx = enemy.x - barrelX;
						int16_t dy = enemy.y - barrelY;
						if (ABS(dx) < blastRadius && ABS(dy) < blastRadius)
						{
							enemy.Damage(enemyBlastDamage);
						}
					}

					{
						int16_t dx = Game::player.x - barrelX;
						int16_t dy = Game::player.y - barrelY;
						if (ABS(dx) < blastRadius && ABS(dy) < blastRadius)
						{
							// barrels hurt but never kill the player outright
							uint8_t dmg = playerBlastDamage;
							if (Game::player.hp <= dmg)
								dmg = Game::player.hp > 1 ? (uint8_t)(Game::player.hp - 1) : 0;
							if (dmg)
								Game::player.Damage(dmg);
						}
					}

					// occasionally the barrel leaves a pickup behind
					switch ((Random() % 6))
					{
					case 0:
						Map::SetCell(cellX, cellY, CellType::Potion);
						break;
					case 1:
						Map::SetCell(cellX, cellY, CellType::Coins);
						break;
					}
					Platform::PlaySound(Sounds::Kill);
				}
				else
				{
					Platform::PlaySound(Sounds::Hit);
				}

				hitAnything = true;
			}
			else
			{
				if (owner == &Game::player)
				{
					Enemy* overlappingEnemy = EnemyManager::GetOverlappingEnemy(p.x, p.y);
					if (overlappingEnemy)
					{
						overlappingEnemy->Damage(Player::attackStrength);

						hitAnything = true;
					}
				}
				else if(Game::player.IsOverlappingPoint(p.x, p.y))
				{
					const EnemyArchetype* enemyArchetype = ((Enemy*)owner)->GetArchetype();
					if (enemyArchetype)
					{
						Game::player.Damage(enemyArchetype->GetAttackStrength());
						if (Game::player.hp == 0)
						{
							Game::stats.killedBy = ((Enemy*)owner)->GetType();
						}
					}
					hitAnything = true;
				}
			}

			if (hitAnything)
			{
				ParticleSystemManager::CreateExplosion(p.x - deltaX, p.y - deltaY);
				p.life = 0;
			}
		}
	}	
}

void ProjectileManager::Init()
{
	for (Projectile& p : projectiles)
	{
		p.life = 0;
	}
}

void ProjectileManager::Draw()
{
	for(Projectile& p : projectiles)
	{
		if (p.life > 0)
		{
			Renderer::DrawObject(p.ownerId == Projectile::playerOwnerId ? projectileSpriteData : enemyProjectileSpriteData, p.x, p.y, 32, AnchorType::BelowCenter);
		}
	}
}
