#ifndef ROSTERINDEXTYPEROLE_H
#define ROSTERINDEXTYPEROLE_H

//IRosterIndex Types
#define RIT_AnyType                                 0
#define RIT_Root                                    1
#define RIT_StreamRoot                              2
#define RIT_Group                                   3
#define RIT_BlankGroup                              4
#define RIT_NotInRosterGroup                        5
#define RIT_MyResourcesGroup                        6
#define RIT_AgentsGroup                             7
#define RIT_Contact                                 8
#define RIT_Agent                                   9
#define RIT_MyResource                              10


//IRosterIndex DataRoles
#define RDR_AnyRole                                 Qt::UserRole
#define RDR_Type                                    Qt::UserRole + 1
#define RDR_Id                                      Qt::UserRole + 2
//XMPP Roles
#define RDR_StreamJid                               Qt::UserRole + 3
#define RDR_Jid                                     Qt::UserRole + 4
#define RDR_PJid                                    Qt::UserRole + 5
#define RDR_BareJid                                 Qt::UserRole + 6
#define RDR_Name                                    Qt::UserRole + 7
#define RDR_Group                                   Qt::UserRole + 8
#define RDR_Show                                    Qt::UserRole + 9
#define RDR_Status                                  Qt::UserRole + 10
#define RDR_Priority                                Qt::UserRole + 11
#define RDR_Self_Show                               Qt::UserRole + 12
#define RDR_Self_Status                             Qt::UserRole + 13
#define RDR_Self_Priority                           Qt::UserRole + 14
#define RDR_Subscription                            Qt::UserRole + 15
#define RDR_Ask                                     Qt::UserRole + 16
//Decoration Roles
#define RDR_HideGroupExpander                       Qt::UserRole + 32
#define RDR_FontHint                                Qt::UserRole + 33
#define RDR_FontSize                                Qt::UserRole + 34
#define RDR_FontWeight                              Qt::UserRole + 35
#define RDR_FontStyle                               Qt::UserRole + 36
#define RDR_FontUnderline                           Qt::UserRole + 37
#define RDR_LabelIds                                Qt::UserRole + 38
#define RDR_LabelOrders                             Qt::UserRole + 39
#define RDR_LabelValues                             Qt::UserRole + 40
#define RDR_LabelFlags                              Qt::UserRole + 41
#define RDR_FooterText                              Qt::UserRole + 42

#endif