#include "manifest.h"

#include <checksum.h>
#include <config_file.h>
#include <context.h>
#include <signature.h>
#include <utils.h>

#define RAUC_IMAGE_PREFIX	"image"

gboolean load_manifest(const gchar *filename, RaucManifest **manifest) {
	RaucManifest *raucm = g_new0(RaucManifest, 1);
	gboolean res = FALSE;
	GKeyFile *key_file = NULL;
	gchar **groups;
	gsize group_count;

	key_file = g_key_file_new();

	res = g_key_file_load_from_file(key_file, filename, G_KEY_FILE_NONE, NULL);
	if (!res)
		goto free;

	/* parse [update] section */
	raucm->update_compatible = g_key_file_get_string(key_file, "update", "compatible", NULL);
	if (!raucm->update_compatible) {
		goto free;
	}
	raucm->update_version = g_key_file_get_string(key_file, "update", "version", NULL);

	/* parse [keyring] section */
	raucm->keyring = g_key_file_get_string(key_file, "keyring", "archive", NULL);

	/* parse [handler] section */
	raucm->handler_name = g_key_file_get_string(key_file, "handler", "filename", NULL);

	/* parse [image.*] sections */
	groups = g_key_file_get_groups(key_file, &group_count);
	for (gsize i = 0; i < group_count; i++) {
		gchar **groupsplit;

		groupsplit = g_strsplit(groups[i], ".", 2);
		if (g_str_equal(groupsplit[0], RAUC_IMAGE_PREFIX)) {
			RaucImage *image = g_new0(RaucImage, 1);
			gchar *value;

			image->slotclass = g_strdup(groupsplit[1]);

			value = g_key_file_get_string(key_file, groups[i], "sha256", NULL);
			if (value) {
				image->checksum.type = G_CHECKSUM_SHA256;
				image->checksum.digest = value;
			}

			image->filename = g_key_file_get_string(key_file, groups[i], "filename", NULL);

			raucm->images = g_list_append(raucm->images, image);

		}
		g_strfreev(groupsplit);
	}

	g_strfreev(groups);

	res = TRUE;
free:
	g_key_file_free(key_file);
	*manifest = raucm;

	return res;
}

gboolean save_manifest(const gchar *filename, RaucManifest *mf) {
	GKeyFile *key_file = NULL;
	gboolean res = FALSE;

	key_file = g_key_file_new();

	if (mf->update_compatible)
		g_key_file_set_string(key_file, "update", "compatible", mf->update_compatible);

	if (mf->update_version)
		g_key_file_set_string(key_file, "update", "version", mf->update_version);

	if (mf->keyring)
		g_key_file_set_string(key_file, "keyring", "archive", mf->keyring);

	if (mf->handler_name)
		g_key_file_set_string(key_file, "handler", "filename", mf->handler_name);

	for (GList *l = mf->images; l != NULL; l = l->next) {
		RaucImage *image = (RaucImage*) l->data;
		gchar *group;

		if (!image || !image->slotclass)
			continue;

		group = g_strconcat(RAUC_IMAGE_PREFIX ".", image->slotclass, NULL);

		if (image->checksum.type == G_CHECKSUM_SHA256)
			g_key_file_set_string(key_file, group, "sha256", image->checksum.digest);

		if (image->filename)
			g_key_file_set_string(key_file, group, "filename", image->filename);

		g_free(group);

	}

	res = g_key_file_save_to_file(key_file, filename, NULL);
	if (!res)
		goto free;

free:
	g_key_file_free(key_file);

	return res;

}

static void free_image(gpointer data) {
	RaucImage *image = (RaucImage*) data;

	g_free(image->slotclass);
	g_free(image->checksum.digest);
	g_free(image->filename);
	g_free(image);
}

void free_manifest(RaucManifest *manifest) {

	g_free(manifest->update_compatible);
	g_free(manifest->update_version);
	g_free(manifest->keyring);
	g_free(manifest->handler_name);
	g_list_free_full(manifest->images, free_image);
	g_free(manifest);
}


static gboolean update_manifest_checksums(RaucManifest *manifest, const gchar *dir) {
	gboolean res = TRUE;

	for (GList *elem = manifest->images; elem != NULL; elem = elem->next) {
		RaucImage *image = elem->data;
		gchar *filename = g_build_filename(dir, image->filename, NULL);
		res = update_checksum(&image->checksum, filename);
		g_free(filename);
		if (!res)
			break;
	}

	return res;
}

static gboolean verify_manifest_checksums(RaucManifest *manifest, const gchar *dir) {
	gboolean res = TRUE;

	for (GList *elem = manifest->images; elem != NULL; elem = elem->next) {
		RaucImage *image = elem->data;
		gchar *filename = g_build_filename(dir, image->filename, NULL);
		res = verify_checksum(&image->checksum, filename);
		g_free(filename);
		if (!res)
			break;
	}

	return res;
}

gboolean update_manifest(const gchar *dir, gboolean signature) {
	gchar* manifestpath = g_build_filename(dir, "manifest.raucm", NULL);
	gchar* signaturepath = g_build_filename(dir, "manifest.raucm.sig", NULL);
	RaucManifest *manifest = NULL;
	GBytes *sig = NULL;
	gboolean res = FALSE;

        g_assert_nonnull(r_context()->certpath);
        g_assert_nonnull(r_context()->keypath);

	res = load_manifest(manifestpath, &manifest);
	if (!res)
		goto out;

	res = update_manifest_checksums(manifest, dir);
	if (!res)
		goto out;

	res = save_manifest(manifestpath, manifest);
	if (!res)
		goto out;

	if (signature) {
		sig = cms_sign_file(manifestpath,
				    r_context()->certpath,
				    r_context()->keypath);
		if (sig == NULL)
			goto out;

		res = write_file(signaturepath, sig);
		if (!res)
			goto out;
	}

out:
	g_clear_pointer(&sig, g_bytes_unref);
	g_clear_pointer(&manifest, free_manifest);
	g_free(signaturepath);
	g_free(manifestpath);
	return res;
}

gboolean verify_manifest(const gchar *dir, gboolean signature) {
	gchar* manifestpath = g_build_filename(dir, "manifest.raucm", NULL);
	gchar* signaturepath = g_build_filename(dir, "manifest.raucm.sig", NULL);
	RaucManifest *manifest = NULL;
	GBytes *sig = NULL;
	gboolean res = FALSE;

        g_assert_nonnull(r_context()->certpath);
        g_assert_nonnull(r_context()->keypath);

	if (signature) {
		sig = read_file(signaturepath);
		if (sig == NULL)
			goto out;

		res = cms_verify_file(manifestpath, sig, 0);
		if (!res)
			goto out;

	}

	res = load_manifest(manifestpath, &manifest);
	if (!res)
		goto out;

	res = verify_manifest_checksums(manifest, dir);
	if (!res)
		goto out;

out:
	g_clear_pointer(&sig, g_bytes_unref);
	g_clear_pointer(&manifest, free_manifest);
	g_free(signaturepath);
	g_free(manifestpath);
	return res;
}
