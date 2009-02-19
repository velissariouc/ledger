/*
 * Copyright (c) 2003-2009, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "iterators.h"
#include "journal.h"
#include "compare.h"

namespace ledger {

void entries_iterator::reset(journal_t& journal)
{
  entries_i   = journal.entries.begin();
  entries_end = journal.entries.end();
  entries_uninitialized = false;
}

entry_t * entries_iterator::operator()()
{
  if (entries_i != entries_end)
    return *entries_i++;
  else
    return NULL;
}

void journal_xacts_iterator::reset(journal_t& journal)
{
  entries.reset(journal);

  entry_t * entry = entries();
  if (entry != NULL)
    xacts.reset(*entry);
}

xact_t * journal_xacts_iterator::operator()()
{
  xact_t * xact = xacts();
  if (xact == NULL) {
    entry_t * entry = entries();
    if (entry != NULL) {
      xacts.reset(*entry);
      xact = xacts();
    }
  }
  return xact;
}

void xacts_commodities_iterator::reset(journal_t& journal)
{
  journal_xacts.reset(journal);

  std::set<commodity_t *> commodities;

  for (xact_t * xact = journal_xacts(); xact; xact = journal_xacts()) {
    commodity_t& comm(xact->amount.commodity());
    if (comm.flags() & COMMODITY_NOMARKET)
      continue;
    commodities.insert(&comm);
  }

  std::map<string, entry_t *> entries_by_commodity;

  foreach (commodity_t * comm, commodities) {
    optional<commodity_t::varied_history_t&> history = comm->varied_history();
    if (! history)
      continue;

    account_t * account = journal.master->find_account(comm->symbol());

    foreach (commodity_t::base_t::history_by_commodity_map::value_type pair,
	     history->histories) {
      foreach (commodity_t::base_t::history_map::value_type hpair,
	       pair.second.prices) {
	entry_t * entry;
	string    symbol = hpair.second.commodity().symbol();

	std::map<string, entry_t *>::iterator i =
	  entries_by_commodity.find(symbol);
	if (i != entries_by_commodity.end()) {
	  entry = (*i).second;
	} else {
	  entry_temps.push_back(new entry_t);
	  entry = entry_temps.back();
	  entry->payee = symbol;
	  entry->_date = hpair.first.date();
	  entries_by_commodity.insert
	    (std::pair<string, entry_t *>(symbol, entry));
	}

	xact_temps.push_back(xact_t(account));
	xact_t& temp = xact_temps.back();
	temp._date  = hpair.first.date();
	temp.entry  = entry;
	temp.amount = hpair.second;
	temp.set_flags(ITEM_GENERATED | ITEM_TEMP);

	entry->add_xact(&temp);
      }
    }
  }

  entries.entries_i   = entry_temps.begin();
  entries.entries_end = entry_temps.end();

  entries.entries_uninitialized = false;

  entry_t * entry = entries();
  if (entry != NULL)
    xacts.reset(*entry);
}

xact_t * xacts_commodities_iterator::operator()()
{
  xact_t * xact = xacts();
  if (xact == NULL) {
    entry_t * entry = entries();
    if (entry != NULL) {
      xacts.reset(*entry);
      xact = xacts();
    }
  }
  return xact;
}

account_t * basic_accounts_iterator::operator()()
{
  while (! accounts_i.empty() &&
	 accounts_i.back() == accounts_end.back()) {
    accounts_i.pop_back();
    accounts_end.pop_back();
  }
  if (accounts_i.empty())
    return NULL;

  account_t * account = (*(accounts_i.back()++)).second;
  assert(account);

  // If this account has children, queue them up to be iterated next.
  if (! account->accounts.empty())
    push_back(*account);

  return account;
}

void sorted_accounts_iterator::sort_accounts(account_t& account,
					     accounts_deque_t& deque)
{
  foreach (accounts_map::value_type& pair, account.accounts)
    deque.push_back(pair.second);

  std::stable_sort(deque.begin(), deque.end(),
		   compare_items<account_t>(sort_cmp));
}

void sorted_accounts_iterator::push_all(account_t& account)
{
  accounts_deque_t& deque(accounts_list.back());

  foreach (accounts_map::value_type& pair, account.accounts) {
    deque.push_back(pair.second);
    push_all(*pair.second);
  }
}

void sorted_accounts_iterator::push_back(account_t& account)
{
  accounts_list.push_back(accounts_deque_t());

  if (flatten_all) {
    push_all(account);
    std::stable_sort(accounts_list.back().begin(),
		     accounts_list.back().end(),
		     compare_items<account_t>(sort_cmp));
  } else {
    sort_accounts(account, accounts_list.back());
  }

  sorted_accounts_i.push_back(accounts_list.back().begin());
  sorted_accounts_end.push_back(accounts_list.back().end());
}

account_t * sorted_accounts_iterator::operator()()
{
  while (! sorted_accounts_i.empty() &&
	 sorted_accounts_i.back() == sorted_accounts_end.back()) {
    sorted_accounts_i.pop_back();
    sorted_accounts_end.pop_back();
    assert(! accounts_list.empty());
    accounts_list.pop_back();
  }
  if (sorted_accounts_i.empty())
    return NULL;

  account_t * account = *sorted_accounts_i.back()++;
  assert(account);

  // If this account has children, queue them up to be iterated next.
  if (! flatten_all && ! account->accounts.empty())
    push_back(*account);

  // Make sure the sorting value gets recalculated for this account
  account->xdata().drop_flags(ACCOUNT_EXT_SORT_CALC);
  return account;
}

} // namespace ledger
