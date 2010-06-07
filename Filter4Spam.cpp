/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,..,2004  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */


/*
   [i_a]

   Additional filter type (a build-in 'external command' really) for spam filtering and
   email redistribution from a POP3 feed.


   Filter process:

   - CAPTCHA feedback check: see if this email is a CATCHA response for a pending CAPTCHA request: if so,
     process in the CATCHA 'learn' section: decode response and update data accordingly. Also instruct the
	 proper CRM stage(s) to 'learn' the given email.

   - CAPTCHA lookup: see if sender is in one of the CAPTCHA databases (good or bad) and augment the email with that info:
     Add a suitable header to the email.

   - Fetch the header section of the email and preprocess it for CRM classification

   - Perform 'stage 1' CRM114 classification. Basically we're only going to look at sender/receiver/subjectline only.
     The 'rough first stage of email triage'.
	 Results (given by the 'pR' output of CRM114) are either good, bad or unsure.

   - 'bad' is propagated to the spam bin. (Maybe we'll add a second CRM stage here too, like in the 'good' case.)

   - 'good' is fed to the next CRM classification stage.

   - 'unsure' is stored in a 'to be scrutinized' bin and a copy is forwarded to The Central Scrutinizer 
     local account for manual inspection.

     A 0.5 (configurable; 0.5 .. 1.5?) day 'sanity delay' is started, after which the CATCHA engine will kick in:
	 If the sender is NOT in any database yet, a CAPTCHA request will be selected and transmitted to the sender.
	 The request is also stored in the 'pending' queue so the CAPTCHA feedback check can validate/correlate against
	 the original request.

     If the local account inspects the email before the CAPTCHA kicks in, the local admin can send a response, which will 
	 be treated as a CAPTCHA response and is processed accordingly: sender will be added to good or bad list.

   - When the 'good' is fed to the second stage, that CRM114 stage will classify the email based on the COMPLETE
     email content (though preprocessed!).

	 This stage will also produce a pR which will assign the email to one of three: good / bad / unsure.

   - 'good' is forwarded to the recipient

   - 'bad' is sent to the spam bin

   - 'unsure' is fed to the CAPTCHA scrutinizer stage above.

     This implies that the CAPTCHA handler must know about the 'origin' of the email, i.e. which filter stage
	 produced the 'unsure' verdict, as a CAPTCHA response will need to (at least) 'learn' that stage so this email
	 will subsequently be classified as good or bad.

	 If a CRM stage 2 is the origin, both stage1 and stage2 CRM need to learn; if stage1 CRM is the origin, only
	 CRM stage1 must learn this email in order to prevent stage2 'polution'. (stage1 is assumed to produce a low
	 quality result; hence the thought to add another stage2 CRM filter to the 'bad' chain too, as there MAY be quite 
	 a few FALSE NEGATIVES in there.

	 A possible alternative solution might be to 'retest' the complete set of previously learned 'bad' samples. This 
	 is costly however, but something like this (periodic rescan of known samples) might be useful to detect
	 CRM 'aging' at an early stage: as things are learned, previous stuff may slowly 'degrade' in there.

	 For this you'll need a complete email cache for, say, the last 3 months. Maybe we can use the CRM reaver cache
	 for that, but we need to store 'evaluation status' (good/bad) with each email too, because that's needed to
	 verify the rescan results.



	 VirusScan
	 =========

	 'unsure' and 'good' email is also scanned for virusses using ClamAV; any virus-containing email is redirected to
	 the virus-collecting mailbox.




	 Filesystem: configfiles, etc.
	 =============================

	 templates\q\qNNN.txt    (template captcha questions / scripts)
	          \a\aNNN.txt    (template captcha answers / scripts)
     pending\qXXX.txt        (who, when, Q, expected A)
	 done\good.txt           (who: the 'good guys')
	     \bad.txt            (who: the 'bad guys'; hopefully none ;-) )

 */
