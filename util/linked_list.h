/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: A series of universal single -connection list operation functions are implemented..
 ******************************************************************************/
#ifndef LINKED_LIST
#define LINKED_LIST
#include <iostream>
#include <cstdio>
#include <cstdlib>

// Function to create a new node
template <typename ListNode>
ListNode* CreateNode()
{
    ListNode* newNode = (ListNode*)malloc(sizeof(ListNode));
    if (newNode == nullptr) {
        return nullptr;
    }
    memset(newNode, 0, sizeof(ListNode));

    newNode->next = nullptr;
    return newNode;
}

// Function to add a node at the head of the linked list
template <typename ListNode>
void addHead(ListNode** head, ListNode* newNode)
{
    newNode->next = *head;
    *head = newNode;
}

// Function to add a node at the tail of the linked list
template <typename ListNode>
void AddTail(ListNode** head, ListNode** newNode)
{
    if (*head == nullptr) {
        *head = *newNode;
    } else {
        ListNode* current = *head;
        while (current->next != nullptr) {
            current = current->next;
        }
        current->next = *newNode;
    }
}

// Function to add a node at the tail of the double linked list
template <typename ListNode>
void AddDoubleLinkedTail(ListNode** head, ListNode** newNode)
{
    if (*head == nullptr) {
        *head = *newNode;
    } else {
        ListNode* current = *head;
        while (current->next != nullptr) {
            current = current->next;
        }
        current->next = *newNode;
        (*newNode)->prev = current;
    }
}

// Function to free the linked list
template <typename ListNode>
void FreeList(ListNode** head)
{
    ListNode* current = *head;
    ListNode* nextNode;

    while (current != nullptr) {
        nextNode = current->next;
        free(current);
        current = nextNode;
    }
    *head = nullptr;  // Ensure the head is set to nullptr after freeing all nodes
}
#endif
