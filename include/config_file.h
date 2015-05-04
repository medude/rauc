#pragma once

#include <glib.h>

#include <checksum.h>

/* System configuration */
typedef struct {
	gchar *system_compatible;
	gchar *system_bootloader;

	gchar *keyring_path;

	GHashTable *slots;
} RaucConfig;

typedef struct _RaucSlot {
	gchar *name;
	gchar *device;
	gchar *type;
	gchar *bootname;
	gboolean readonly;
	struct _RaucSlot *parent;
} RaucSlot;

typedef struct {
} RaucSlotGroup;

typedef struct {
	gchar* slotclass;
	RaucChecksum checksum;
	gchar* filename;
} RaucImage;

typedef struct {
	gchar *status;
	RaucChecksum checksum;
} RaucSlotStatus;

gboolean load_config(const gchar *filename, RaucConfig **config);
void free_config(RaucConfig *config);

gboolean load_slot_status(const gchar *filename, RaucSlotStatus **slotstatus);
gboolean save_slot_status(const gchar *filename, RaucSlotStatus *slotstatus);
void free_slot_status(RaucSlotStatus *slotstatus);
