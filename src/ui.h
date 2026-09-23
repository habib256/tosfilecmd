/*
 * ui.h -- boites de dialogue, saisie, barre de progression, erreurs critiques.
 */
#ifndef UI_H
#define UI_H

/* Boite avec lignes de texte et boutons ; la premiere lettre d'un bouton
 * est son raccourci. Renvoie l'indice du bouton, ou -1 (ESC). */
int ui_dialog(const char *title, const char *const *lines, int nlines,
              const char *const *buttons, int nbuttons, int def);
void ui_message(const char *title, const char *l1, const char *l2);
/* 1 = oui. def_yes choisit le bouton par defaut (RETURN). */
int ui_confirm(const char *title, const char *l1, const char *l2, int def_yes);
/* Saisie d'une ligne dans buf (cap octets, 0 final compris). 1 = validee. */
int ui_input(const char *title, const char *prompt, char *buf, int cap);
void ui_error(const char *what, const char *path, long err);

void ui_progress_open(const char *title);
void ui_progress(const char *name, unsigned long done, unsigned long total,
                 long files_done, long files_total);
void ui_progress_close(void);

/* Gestionnaire etv_critic : a installer pendant tout le programme. */
void ui_critic_install(void);
void ui_critic_remove(void);

#endif
