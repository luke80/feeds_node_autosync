<?php

/**
 * Implements hook_form_alter().
 *
 * Modifies the "node_form" form for any form id ending in "_node_form" (per the initial if). Modifies the form element options to remove editability for protected fields per the node_type settings.
 *
 * @param form
 *   The form array, containing the "current" state of the form. Passed by reference so that modifications are carried.
 * @param form_state
 *   A keyed array containing the current state of the form. Passed by reference so that modifications are carried.
 * @param form_id
 *   String representing the name of the form itself. This is the "FORM_ID" part of the name of the function.
 */
function feeds_node_autosync_form_alter( &$form, &$form_state, $form_id ) {
	//	Required: type, with a #value, to match the form_alter function name
	if (isset($form['type']) && isset($form['type']['#value']) && $form['type']['#value'] . '_node_form' == $form_id) {
		$c = feeds_node_autosync_config($form['#node']->type,NULL,TRUE);		//	Retrieve the configuration
		//if($c->config->active->value != 1) return TRUE;	//	This line would eliminate the possibility of using this module simply to disallow edits to specific fields. The form alter function would terminate here, before it began.
		if(is_object($c->config->block_field_edits->value))
			$block_edits = (array) get_object_vars($c->config->block_field_edits->value);	//	Store the field ids to "block"
		elseif(is_array($c->config->block_field_edits->value))
			$block_edits = $c->config->block_field_edits->value;
		else
			$block_edits = array();
		$block_edits_nested = array();
		$ignore_keys = array(	//	Some of these make no sense to parse, let's ignore them...
				"#node_edit_form",
				"#attributes",
				"#node",
				"#validate",
				"#submit",
				"#parents",
				"#entity",
		);
		$disable_field_names = array();		//	In the interest of keeping the user informed, collect the human-readable titles for messages later on.
		$fields = field_info_instances('node',$form['#node']->type);
		$remove_items_block_edits = array();
		foreach($block_edits as $field_name) {	//	loop over the fields to block edits upon
			$fii = field_info_instance('node', $field_name, $form['#node']->type);
			$fii = $fii['label'];	//	additional fields are retrievable with this function. However, it does miss some things.
			if($fii) {
				$disable_field_names[] = $fii;
			} elseif($field_name == 'title') {	//	Disabling the titles is still buggy. Not sure why yet.
				$types = node_type_get_types();
				if (isset($types[$form['#node']->type]) && $types[$form['#node']->type]->has_title) {
					$disable_field_names[] = $types[$form['#node']->type]->title_label;
				} else {
					drupal_set_message(t('Failed to block edits on the title. It looks like %content_type doesn\'t have one?',array('%content_type'=>node_type_get_name($form['#node']->type))),'warning');
				}
				unset($types);
			} elseif(is_object($c->config->block_field_edits->value) && property_exists($c->config->block_field_edits->value,$field_name) && $field_name == $c->config->block_field_edits->value->{$field_name}) {
				//	get_object_vars was meant to give me an array of exactly this structure, however occasionally something gets mixed up. First we verify.
				if(strpos($field_name,":") > -1) {	//	if the name contains a ":" it means we're targetting a sub-field, referred to here as "nested"!
					$remove_items_block_edits[] = $field_name;	//	remove this item from the structure... later
					$field_parent = substr($field_name,0,strpos($field_name,":"));
					$field_remainder = substr($field_name,strpos($field_name,":")+1);
					if(array_key_exists($field_parent,$fields))
						$field_label = $fields[$field_parent]['label'];
					else
						$field_label = $field_parent;
					$disable_field_names[] = t("@title",array("@title"=>$field_label.":".$field_remainder));
					//	Now build this information in to the block_edits_nested structure
					if(!array_key_exists($field_parent,$block_edits_nested))
						$block_edits_nested[$field_parent] = array();
					$block_edits_nested[$field_parent][$field_remainder] = $field_remainder;
					unset($field_parent);
					unset($field_remainder);
					unset($field_label);
				}
			}
			unset($fii);
		}
		$block_edits = array_diff($block_edits,$remove_items_block_edits);	//	This is that part that removes the thinks from the structure.
		if(count($disable_field_names) > 0) {
			//	Now let the user know some things are disabled by this module.
			drupal_set_message(t("The following fields are managed by a remote source with Feeds Auto-Sync, and can't be modified;<br/>%field_names !exception",array('%field_names'=>implode(", ",$disable_field_names),"!exception"=>((user_access('fna edit protected fields')?"<br/>You can edit these fields, however they will probably be overwritten by the Auto-sync.":"")))),"warning");
			//	Parse through the form data to add the '#disabled' flag to matching keys.
			_feeds_node_autosync_traverse_form_data("root",$form,$block_edits,$ignore_keys,$block_edits_nested);
			//	Block edits is passed by reference, so it should now be enpty. If not, warn the user.
			if(count($block_edits) > 0) {	//	This isn't an error, it is more of a warning - and hopefully the problem is corrected with the link provided.
				drupal_set_message(t('Something strange has happened. A configured protected field didn\'t exist in this form. Please verify the %content_type is configured properly with Feeds Auto-Sync.<br/><a href="@feed_map_url">Change the mappings of %feed_human_name (which drives %content_type) by clicking here.</a>',array('%feed_human_name'=>$c->config->importer_config['name'],'%content_type'=>node_type_get_name($form['#node']->type),'@feed_map_url'=>url('admin/structure/feeds/'.$c->config->importer->value.'/mapping'))),'warning');
				drupal_set_message(t("remaining field! ".join(", ",$block_edits)));
			}
		}
		//	Since the feed is enforced to contain this information before the settings are validated, simplify the node edit form by removing extra fetcher options.
		if(array_key_exists('feeds',$form) && array_key_exists('FeedsHTTPFetcher', $form['feeds'])) {
			_feeds_node_autosync_disable_formfield($form['feeds'],NULL,'This field is configured to be protected from edits. You are able to edit it due to elevated permissions.');
			//	The 'feeds' section doesn't need individual parser settings here.
			foreach($form['feeds'] as $key=>$value) {
				if(preg_match("/^Feeds.+Parser\$/",$key) != 0) unset($form['feeds'][$key]);
			}
		}
		unset($c);
		unset($block_edits);
		unset($block_edits_nested);
		unset($ignore_keys);
		unset($disable_field_names);
		unset($fields);
		unset($remove_items_block_edits);
	}
}

/**
 * Implemented by the above form_altar hook.
 *
 * Recursive search through the form array, disabling "blocked edit" fields.
 *
 * @param key
 *   the key of the branch of form currently being parsed.
 * @param form
 *   reference-passed form so this function can change it.
 * @param block_edits
 *   An array containing the names of the form elements to disable. Since each key is removed once processed, this is passed by reference.
 * @param ignore_keys
 *   An array containing the names of keys to ignore (because they are never form elements)
 * @param nested_keys
 *   A multi-dimensional array containing the names of keys, and the sub-keys to match and ignore. The keys are removed once processed, just like block_edits.
 */
function _feeds_node_autosync_traverse_form_data($key,&$form,&$block_edits,$ignore_keys=array(),&$nested_keys=array()) {
	if(!is_numeric($key)) {	//	non-numeric keys are removed once...
		if(in_array($key,$block_edits)) {	//	Has this one been done? (Does it need to be?)
			_feeds_node_autosync_disable_formfield($form,'This field is not editable because it is synced from a different source.','This field is configured to be protected from edits. You are able to edit it due to elevated permissions.');
			$block_edits = array_diff($block_edits, array($key));	//	Remove this from the list (keys processed once)
		}
		if(array_key_exists($key,$nested_keys)) {	//	Is this a nested key we're looking for?
			$nested_ignore_keys = $nested_keys[$key];
			$children = element_children($form);
			foreach($children as $childkey) {
				_feeds_node_autosync_traverse_form_data($key,$form[$childkey],$nested_ignore_keys,$ignore_keys);
			}
			unset($children);
		}
	}
	//	Detect for and execute required recursion (to find all the form members)
	if((!in_array($key, $ignore_keys) || (is_array($form) && is_numeric($key))) && (count($block_edits) > 0 || count($nested_keys) > 0)) {
		if(is_array($form)){
			$children = element_children($form);
			if( is_array($children) && count($children) > 0 )
			foreach($children as $childkey) {
				_feeds_node_autosync_traverse_form_data($childkey,$form[$childkey],$block_edits,$ignore_keys,$nested_keys);
			}
			unset($children);
		}
	}
}

/**
 * Implemented by functions which need to turn off the editability of form fields (E.g. the function above which recurses to look for them)
 *
 * @param formfield
 *   reference-passed form field so this function can change it.
 * @param notallowed_message
 *   A string message to add to the title of the element to give the user information about what's going on
 * @param exceptionally_allowed_message
 *   A string message to add to the title of the element to give the user information about what's going on
 */
function _feeds_node_autosync_disable_formfield(&$formfield, $notallowed_message=NULL, $exceptionally_allowed_message=NULL) {
	//	Perhaps if this key doesn't exist we ought to simply ignore it... but this also would work.
	if(!array_key_exists('#attributes',$formfield))
		$formfield['#attributes'] = array();
	//	Sometimes there aren't already classes... perhaps there are.
	if(!array_key_exists('class',$formfield['#attributes']) || !is_array($formfield['#attributes']['class']))
		$formfield['#attributes']['class'] = array();
	//	This class is picked up by the module stylesheet. Change the styling there, please.
	$formfield['#attributes']['class'][] = "feeds-node-autosync-disabled";
	if(!user_access('fna edit protected fields')) {	//	Check permissions
		$formfield['#disabled'] = TRUE;	//	Basic no-access flag on the form input.
		if($notallowed_message)
			$formfield['#attributes']['title'] = t($notallowed_message);
	} else { // if(array_key_exists("#attributes",$form)) {	//	If they can edit it, there should still be some level of indication that something is different.
		$formfield['#attributes']['class'][] = 'feeds-node-autosync-adminaccess';	//	This should color it, highlight it, or whatever to give the user the impression that it might not be editable. When they try - they can... but they still shouldn't.
		if($exceptionally_allowed_message)
			$formfield['#attributes']['title'] = t($exceptionally_allowed_message);
	}
}

/**
 * Implements hook_FORM_ID_form_alter().
 *
 * Modifies the "node_type" form. Adds configuration options to the node type for auto-sync of content of this type.
 *
 * @param form
 *   The form array, containing the "current" state of the form.
 * @param form_state
 *   A keyed array containing the current state of the form.
 * @param form_id
 *   String representing the name of the form itself. This is the "FORM_ID" part of the name of the function.
 */
function feeds_node_autosync_form_node_type_form_alter( &$form, &$form_state, $form_id ) {
	//	Create the new tab (appears at the bottom of the form, probably under "Comment settings"
	$form['feeds_auto_sync'] = array(
		 '#type' => 'fieldset',
		 '#title' => t('Feeds Auto-Sync'),
		 '#collapsible' => TRUE,
		 '#collapsed' => TRUE,
		 '#group' => 'additional_settings',
	);
	if(user_access('fna bundle configuration form')) {
		//	Add the submission and validation callbacks to the form.
		array_push($form['#submit'], 	'feeds_node_autosync_nodetypeform_data');
		array_push($form['#validate'], 	'feeds_node_autosync_save_node_type_form_validate');
		//	Retrieve the configuration object..
		$c = feeds_node_autosync_config($form['#node_type']->type);

		//	Initialize the $form_datas to contain all the requisite data places
		$form_datas = array(
				'config_problems'		=> array(),		//	This array is populated with helpful text to assist the user to configure the feed to work with this module.
				'importers' => array(
					'all'	=> array(),	//	This array is populated with all the form importers found, and holds a boolean value which is true if this importer is valid for auto-syncing the nodes.
					'valid'		=> array(),	//	This array is populated with the form importers that are valid for use.
					'fields'	=> array(),	//	This array is populated with the form fields that are mapped specifically in this importer.
				),
				'importer_mapped_fields'	=> array(),	//	This array is populated with the mapped fields that are configured, and might need to be disabled to prevent modification to fields intended to be driven only by the feed.
				'importer_mapped_fields_messages'	=> array(),	//	This array is populated with messages from mapped fields that are configured
		);
		$general_errors = FALSE;	//	Start with the hope that there are no general errors (but there might be later on)
		//	Output a wrapper for the errors (there is a corresponding "close" for this wrapper later on)
		$form['feeds_auto_sync']['fna_error_section_begin']  = array(
			'#markup' => '<section class="fna-feedback"><div>',
		);
		if($form['#node_type']->module != "" && $form['#node_type']->module != "node") {
			//	First general error! The type needs to be "node"
			_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t("Sorry, the type needs to be 'node', not %module_name.",array('%module_name',$form['#node_type']->module)));
			$general_errors = TRUE;
		} else {
			//	The importer needs to be configured for the node type loaded...
			foreach(feeds_importer_load_all() as $importer) {
				$validation_data = feeds_node_autosync_validate_importer($importer->id, $form['#node_type']->type);
				$form_datas['importers']['all'] =		array_merge($form_datas['importers']['all'], $validation_data['all']);
				$form_datas['importers']['valid'] =		array_merge($form_datas['importers']['valid'], $validation_data['valid']);
				$form_datas['importers']['fields'] =	array_merge($form_datas['importers']['fields'], $validation_data['mapped_fields']);
				$form_datas['importer_mapped_fields'] =	array_merge($form_datas['importer_mapped_fields'], $validation_data['mapped_fields']);
				$form_datas['importer_mapped_fields_messages'] =	array_merge($form_datas['importer_mapped_fields_messages'], $validation_data['mapped_fields_messages']);
			}
		}
		//	Verify successful import of nodes with 'FEED_URL' set in db.
		$missing_nodes_error = t('Before connecting auto-update to your %bundle, your content usually would be created (or updated) by a feed. Your Feeds importer needs to be run at least once to establish this connection.',array('%bundle'=>node_type_get_name($form['#node_type']->type)));		//	<div><strong>No&nbsp;\'feed_source\'&nbsp;data&nbsp;found.</strong> This is required to map. Otherwise the individual items won\'t have a location to sync from.</div>
		foreach($form_datas['importers']['valid'] as $importer_name=>$importer) {
			//	Provide an interface option to "configure" one of the available importers with the required settings.
			//	These instructions are slightly deprecated now that the "override" form is available.
			if(strlen($form_datas['importers']['all'][$importer_name]['bundle']) == 0) {
				$missing_nodes_error .= " ".l(t('Click here to go import page for @importer.',array('@importer'=>$importer)),'import/'.$importer_name).t(" (Please don't forget to change this back to the content type when you're done.)");
			} else {
				$missing_nodes_error .= t(" This is confusing, and I'm worry about that. To get your feed data initialized the first time, it needs to be imported without an attached node with the standalone form. After successfully importing that way, import it once more after re-attaching it to your intended bundle. ").l(t("Click here to go to the form for @importer",array('@importer'=>$importer)),'admin/structure/feeds/'.$importer_name.'/settings');
			}
		}
		//	Collect node ids to detect if there are feeds_source rows...
		$form_datas['nids'] = db_select('node', 'n')
			->fields('n', array('nid'))
			->fields('n', array('type'))
			->condition('n.type', $form['#node_type']->type)
			//	->condition('n.status', 1)	//	Don't require items to be published (why would this be needed?)
			->execute()
			->fetchCol(); // returns an indexed array
		if(count($form_datas['nids']) > 0 && count($form_datas['config_problems']) == 0) {
			$result = db_select('feeds_source', 'fs')
				->fields('fs', array('id'))
				->fields('fs',array('source'))
				->condition('fs.feed_nid', $form_datas['nids'])
				->condition('fs.source', '', $operator = '<>')
				->range(0,1)
				->execute();
			if($result->rowCount() == 0) {
				$general_errors = TRUE;
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],$missing_nodes_error);
			}
		} else if(count($form_datas['nids']) == 0) {
			//	No nodes means no import!
			$general_errors = TRUE;
			_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],"<div>".t("<strong>No %content_type nodes have been created.</strong> ",array("%content_type"=>node_type_get_name($form['#node_type']->type))).$missing_nodes_error."</div>");
		}
		unset($form_datas['nids']);
		unset($result);
		unset($missing_nodes_error);
		
		if(count($form_datas['importers']['all']) == 0) {	//	First problem to identify. No importers!
			_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<p class="error">You don\'t have an importer set up yet. To activate Auto-sync with Feeds, create an importer for %content_type first.</p>',array('%content_type'=>node_type_get_name($form['#node_type']->type).((substr(node_type_get_name($form['#node_type']->type),-1) != "s")?"s":""))).'<div>'.l('Click here to create a new Feeds importer.','admin/structure/feeds/create').'</div>');
			$general_errors = TRUE;
		} elseif(count($form_datas['importer_mapped_fields']) == 0) {	//	Second problem to identify. No fields mapped to update in this content type.
			$general_errors = TRUE;
			_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<p class="error">It appears that your <em>processor</em> hasn\'t been mapped yet. As you resolve this remember you are also required to have at least one unique column within the content type %content_type, and the processor set to <em>update existing nodes</em>.',array('%content_type'=>node_type_get_name($form['#node_type']->type))));
		} else {
			if(count($form_datas['importers']['valid']) == 0 || TRUE) {	//	Third problem to identify. No valid importers!
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<div class="fna-feedback-header fna-subhead">Feed-specific validation errors</div>'));
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<p class="error">Found %num_feeds available feed sources, but none passed validation.</p>',array('%num_feeds'=>count($form_datas['importers']['all']))));	//	<br/>Your configured feed needs to have a <em>feed_source</em> set. You do this by setting one of the import fields mapped to <em>feed_source</em>. It also helps to make the data provided function as a sync source for the specific node it creates/updates.
			}
			//	These arrays are used to produce the override form
			$override_options = array();
			$feed_urls = array();
			foreach($form_datas['importers']['all'] as $key=>$unvalidated_importer) {
				//	First output the help feedback error text stuff.
				if(array_key_exists('fail_messages', $unvalidated_importer)) {
					if(count($form_datas['importers']['valid']) == 0) {
						_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],'<div class="fna-subhead fna-importer-link">'.l($unvalidated_importer['name'],'admin/structure/feeds/'.$key.'/mapping').'</div>');
					}
					foreach($unvalidated_importer['fail_messages'] as $fail_message) {
						_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<div>'.$fail_message.'</div>'));
					}
				}
				//	If there are no mappings errors, then we can override the settings and automate the initial import!
				if(!$unvalidated_importer['mappings_errors']) {
					//	There are no mapping errors, settings can be overridden with validation handler!
					$override_options[$key] = $unvalidated_importer['name'];
					$feed_urls[$key] = $unvalidated_importer['url'];
				}
			}
			//	If there are overridable importers, then output the options and UI here!
			if(count($override_options) > 0 && count($form_datas['config_problems']) > 0) {
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<br/><br/><div class="fna-subhead">Here\'s some help, if you want it:</div>'));
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],"Automatically correct errors and run an initial import with one of these importers:",
					array(
						"#type"=>'radios',
						'#title' => t('Override settings and run initial batch import'),
						'#description' => t('Select this and submit the form to change all the settings on the selected importer (as needed) and run an import to populate your %bundle nodes.',array('%bundle'=>node_type_get_name($form['#node_type']->type))),
						'#required' => FALSE,
						'#options'	=>	$override_options,
						'#attributes'=>	array('onchange'=>"(function(el){var urls=".json_encode($feed_urls).";document.querySelector('input[data-feedurl=selection-anchor-for-feedurl]').setAttribute('value',(urls[el.getAttribute('value')]!=null)?urls[el.getAttribute('value')]:'');})(this)"),
					),
					"fna_override_importer"
				);
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],"What URL should this initial import request from?",
					array(
						"#type"=>'textfield',
						'#title' => t('URL for Feed data'),
						'#description' => t('Selecting an importer and leaving this field blank will cause the settings to be overridden, but will not instigate an import.'),
						'#required' => FALSE,
						'#default_value' => "",
						'#attributes'=>	array('data-feedurl'=>'selection-anchor-for-feedurl'),
					),
					"fna_override_url"
				);
			} elseif(count($form_datas['config_problems']) > 0) {
				//	Because the override is so helpful (potentially) we should placehold it and encourage the user to know about it and use it when available!
				_feeds_node_autosync_error_feedback($form,$form_datas['config_problems'],t('<br/><br/><div class="fna-subhead">If you correct your mapping-related errors, there is some automatic help available to run your initial import. Fix those mapping errors and come back here to take advantage of it.</div>'));
			}
		}
		//	Close the wrapper created above
		$form['feeds_auto_sync']['fna_error_section_end']  = array(
			'#markup' => '</div></section>',
		);
		/*
		 *	Finally! Begin modifications to the form
		*/
		//	If there are errors to display, display them.
		if(count($form_datas['config_problems']) != 0) {
			//	This help text is very "version 1" and should probably be edited to be clearer and shorter. For now, though, it's probably fine.
			$help_elements = array();
			$help_elements[] =	t('<div class="fna-feedback-header fna-feedback-help">Here\'s some help</div><p class="fna-feedback-help">To auto-sync your %content_type nodes, there must be a Feed set up which the auto-sync can use. There are a number of required mappings (columns, records, or other from your remote data which are imported into defined locations inside your %content_type nodes). Displayed above are specific errors generated because of a problem or problems with your configured Feeds.</p>',array('%content_type'=>node_type_get_name($form['#node_type']->type)));
			$help_elements[] =	t('<p class="fna-feedback-help">It is recommended that you set up two separate Feeds importers with the following purposes:</p>');
			$help_elements[] =	t('<ul class="fna-feedback-help"><li>Main "all records" feed, which updates/imports all your content together (probably on a schedule).</li><li>Secondary "single records" feed, which is driven by a source which is specifically configured to provides information about only the one record reflected in the attached node.</li></ul>');
			$help_elements[] =	t('<p class="fna-feedback-help">To help all of this make sense to you; to use only one importer you would have to create all your %content_type items by hand, and specify the URL which "Feeds" them upon creation. But, if the data exists in a "Feedable" way, then you probably want to both create <em>and</em> update them with a feed. However, those are two distinct purposes and you need separate and distinct feeds to accomplish this. For this reason it is required to have both "feeds_source" and "url" destinations mapped in your processor.</p>',array('%content_type'=>node_type_get_name($form['#node_type']->type)));
			$help_elements[] =	t('<p class="fna-feedback-help">Also, since this process is built on top of a functioning Feed, you need to have used a Feed to create/update at least one of your %content_type items already. For final validation, the content type needs to be bound to the node form, but to perform the "manual feed" you\'ll need to temporarily detach it from that form (as in use the "standalone form" option) and then change it back after manually importing once.</p>',array('%content_type'=>node_type_get_name($form['#node_type']->type)));
			if($general_errors) {
				array_unshift($form['feeds_auto_sync'],array('general_validation_errors'=>array('#markup'=>t('<section class="fna-feedback"><div class="fna-feedback-header fna-subhead">General validation errors</div></section>'))));
			}
			array_unshift($form['feeds_auto_sync'],array('general_errors'=>array('#markup'=>t('<section class="fna-feedback"><div class="fna-feedback-header">Validation errors</div></section>'))));
			$form['feeds_auto_sync']['fna_errors']  = array(
					'#markup' => t('<section class="fna-feedback"><input type="checkbox" id="fna-show-help"><div class="fna-help">'.implode($help_elements,'</div><div class="fna-help">').'</div><label for="fna-show-help">help</label></section>'),
			);
			//	Since the form settings don't matter if they can't work, hide them.
			return true;
		}
		unset($general_errors);
		//	Main control setting. Destination: `node_type`.`auto_sync_toggle`
		$form['feeds_auto_sync']['fna_active']  = array(
				'#type' => 'checkbox',
				'#title' => t('Auto-sync this content type?'),
				'#description' => t('With this checked, when items of this type are modified they will trigger a re-import from the configured source.'),
		);
		if($c->config->active->value == 1) {
			$form['feeds_auto_sync']['fna_active']['#attributes'] = array('checked'=>'checked');
		}
		if(!user_access('fna bundle protect fields form')) {
			if(!array_key_exists("#attributes",$form['feeds_auto_sync']['fna_active']) || !is_array($form['feeds_auto_sync']['fna_active']['#attributes']))
				$form['feeds_auto_sync']['fna_active']['#attributes'] = array();
			$form['feeds_auto_sync']['fna_active']['#attributes']['disabled'] = 'disabled';
		}
		//	Secondary control. Destination: parameters within `node_type`.`auto_sync_config`
		$form['feeds_auto_sync']['fna_importer']  = array(
				'#type' => 'select',
				'#options' => $form_datas['importers']['valid'],
				'#title' => t('Which importer do you want the auto-sync to use?'),
				'#description' => t('This selection contains only importers that passed the validation steps.'),
				'#default_value' =>	t($c->config->importer->value),
		);

		//	Secondary control. Destination: parameter within `node_type`.`auto_sync_config`
		$form['feeds_auto_sync']['fna_cooldown']  = array(
				'#type' => 'select',
				'#options' => drupal_map_assoc(array(5,10,15,20,25,30,45,60)),
				'#title' => t('Cooldown delay on re-importing nodes.'),
				'#description' => t('This setting has two purposes, first to defeat recursion (which is triggered when an auto-update modifys a node, causing it to re-update), second to make the sync resistant to causing an importer lock if two users edit the same node at the same (ish) time.'),
				'#default_value' =>	$c->config->cooldown->value,
		);
		//	Secondary control. Destination: parameter within `node_type`.`auto_sync_config`
		$form['feeds_auto_sync']['fna_block_field_edits']  = array(
				'#type' => 'checkboxes',
				'#options' => $form_datas['importer_mapped_fields'],
				'#title' => t('Block edits on these fields'),
				'#description' => t('Check the node members you wish to make un-editable with the node form.'),
				'#default_value' => array_keys(get_object_vars($c->config->block_field_edits->value)),
		);
		//	If the user lacks appropriate permissions, show the field but don't allow changes
		if(!user_access('fna bundle protect fields form')) {
			if(!array_key_exists("#attributes",$form['feeds_auto_sync']['fna_block_field_edits']) || !is_array($form['feeds_auto_sync']['fna_block_field_edits']['#attributes']))
				$form['feeds_auto_sync']['fna_block_field_edits']['#attributes'] = array();
			$form['feeds_auto_sync']['fna_block_field_edits']['#attributes']['disabled'] = 'disabled';
		}
		//	If there are messages, display them.
		if(count($form_datas['importer_mapped_fields_messages']) > 0) {
			$form['feeds_auto_sync']['fna_block_field_edits_messages']  = array(
					'#markup' => '<section class="fna-feedback"><div>'.implode("</div><div>",$form_datas['importer_mapped_fields_messages']).'</div></section>',
			);
		}
	} else {
		//	Show the message that the user doesn't have sufficient permission
		$form['feeds_auto_sync']['fna_errors']  = array(
				'#markup' => t('<section class="fna-feedback">You lack permission to configure the Auto-sync settings.</section>'),
		);
	}
}

/**
 * Implemented by "feeds_node_autosync_form_node_type_form_alter", by adding this function to that form's "#submit" array.
 *
 * The columns added to the schema and db recieve their data from the form with this function.
 *
 * @param form
 *   The form array, containing the "current" state of the form.
 * @param form_state
 *   A keyed array containing the current state of the form.
 */
function feeds_node_autosync_nodetypeform_data ( $form, &$form_state ) {
	//	The 'fna_active' field is slightly vestigal at this point, but it doesn't hurt much.
	if(array_key_exists('fna_active',$form_state['values'])) {
		$num_updated = db_update('node_type')
			->fields(array('auto_sync_toggle' => $form_state['values']['fna_active']))
			->condition('type', $form['#node_type']->type)
			->execute();
		//	load the configuration object!
		$c = feeds_node_autosync_config($form['#node_type']->type);
		foreach($form_state['values'] as $form_field=>$form_value) {
			//	This changes the prefixed 'fna_' fields into their appropriate key within the configuration object.
			if(substr($form_field,0,4) == 'fna_') {		//	First - only use the proper prefix...
				$field = substr($form_field,4);		//	The name within the configuration object doesn't need the prefix.
				if(!is_null($form_value)) {		//	Setting values to NULL should delete them (or fail to set them)
					if(is_array($form_value)) {	//	Arrays need to be looped through, this is currently most important for the "uneditable fields" settings.
						$values = array();
						foreach($form_value as $key=>$value) {	//	Build it out!
							if($value != "0") {
								$values[$key] = $value;
							} else {
								//$values[$key] = FALSE;
							}
						}
						$c->config->{$field}->value = $values;	//	It's ready now for prime-time!
					} elseif(is_numeric($form_value)) {
						$c->config->{$field}->value = intval($form_value);	//	Integers are pretty easy. Store it as an int!
					} else
						$c->config->{$field}->value = $form_value;	//	Otherwise don't do anything special.
				}
				unset($field);
			}
		}
		feeds_node_autosync_config($form['#node_type']->type, $c);	//	This saves the config to the database.
		unset($c);
		return array($form, $form_state);	//	This provides the changes to the UI.
	}
}

/**
 * Implemented by "feeds_node_autosync_form_node_type_form_alter", by adding this function to that form's "#validate" array.
 *
 * The columns added to the schema and db are validated pre-save with this function.
 *
 * @param form
 *   The form array, containing the "current" state of the form.
 * @param form_state
 *   A keyed array containing the current state of the form.
 */
function feeds_node_autosync_save_node_type_form_validate ( $form, &$form_state ) {
	if(user_access('fna bundle configuration form')) {	//	Validate that the user has access to the form
		//	Override "help" form options
		if(array_key_exists('fna_override_importer',$form_state['values'])) {
			_feeds_node_autosync_force_importer_settings($form_state['values']['fna_override_importer'],$form['#node_type']->type);
		}
		if(array_key_exists('fna_override_url',$form_state['values']) && $form_state['values']['fna_override_url']) {
			_feeds_node_autosync_force_import($form_state['values']['fna_override_importer'],$form_state['values']['fna_override_url']);
		}
		
		//	Validate 'fna_active'
		if(array_key_exists('fna_active',$form_state['values']) && !is_numeric($form_state['values']['fna_active']))
			$form_state['values']['fna_active'] = intval($form_state['values']['fna_active']);
	
		//	Validate 'fna_importer'
		if(array_key_exists('fna_importer',$form_state['values'])) {
			$validation_data = feeds_node_autosync_validate_importer($form_state['values']['fna_importer'], $form['#node_type']->type);
			if(!in_array($form_state['values']['fna_importer'],array_keys($validation_data['valid']),TRUE)) {
				drupal_set_message('Importer failed validation.','warning',TRUE);
				$form_state['values']['fna_importer'] = null;
			}
		}
	
		//	Validate 'fna_cooldown'
		if(array_key_exists('fna_cooldown',$form_state['values'])) {
			if(!is_numeric($form_state['values']['fna_cooldown']))
				$form_state['values']['fna_cooldown'] = intval($form_state['values']['fna_cooldown']);
			if($form_state['values']['fna_cooldown'] < 1)
				$form_state['values']['fna_cooldown'] = 1;
			if($form_state['values']['fna_cooldown'] > 60)
				$form_state['values']['fna_cooldown'] = 60;
		}
		
		if(user_access('fna bundle protect fields form') && array_key_exists('fna_block_field_edits',$form_state['values'])) {
			//	Validate 'fna_block_field_edits'
			//		There's a potential UI frustration here; if the user wants to block edits on a field not mapped in the importer they chose, this will disallow them to do so.
			//		The reason for this is that if the field isn't controlled by the import, blocking edits using the form will render this field useless for most purposes.
			//		I'm considering making this validation rule optional in the module configuration options (which don't exist yet)
			$valid_fields = array();
			foreach($form_state['values']['fna_block_field_edits'] as $field_name=>$field_value) {
				if(in_array($field_name,array_keys($validation_data['mapped_fields']))) {
					$valid_fields[$field_name] = $field_value;
				}
			}
			$form_state['values']['fna_block_field_edits'] = $valid_fields;
			unset($valid_fields);
		} else {
			//	No permission means this value, if provided, should be removed.
			unset($form_state['values']['fna_block_field_edits']);
		}
	} else {
		//	No permission means these values, if provided, should be removed.
		$fields = array('fna_active','fna_importer','fna_cooldown','fna_block_field_edits');
		foreach($fields as $field_name) {
			if(array_key_exists($field_name,$form_state['values'])) {
				unset($form_state['values'][$field_name]);
			}
		}
		unset($fields);
	}
}

/**
 * Implemented by the above _form_alter hook and "feeds_node_autosync_save_node_type_form_validate" by implicit call
 *
 * This function takes an importer name and validates its settings
 *
 * @param importer_id
 *   The string id of the importer
 * @param node_type
 *   The node id string
 *   
 * returns an array indexed with 'all', 'valid', 'mapped_fields' and 'mapped_fields_messages' containing data about the configured feeds.
 */
function feeds_node_autosync_validate_importer($importer_id, $bundle) {
	//	$ret will be returned, these are the keys that should always be there. These structures are merged elsewhere using these keys. The structure here must match the aggregation there.
	$ret = array('all' => array(), 'valid' => array(), 'mapped_fields' => array(), 'mapped_fields_messages'=>array());
	//	Load the importer and the config thereof
	$importer = feeds_importer($importer_id);
	$importer_config = $importer->getConfig();
	$importer_config['id'] = $importer_id;
	unset($importer);	//	Since this is no longer needed, why not delete it?
	//	Check and enforce that the node type (bundle) matches the current, and that the feed is set to 'update existing nodes'.
	//	Also required is at least one field processed as a unique identifier.
	$ret['all'][$importer_config['id']] = array();
	$ret['all'][$importer_config['id']]['FEED_URL_mapped'] = FALSE;
	$ret['all'][$importer_config['id']]['processor_includes_unique_field'] = FALSE;
	$ret['all'][$importer_config['id']]['fields'] = array();
	$ret['all'][$importer_config['id']]['fields_messages'] = array();
	$ret['all'][$importer_config['id']]['fail_messages'] = array();
	$ret['all'][$importer_config['id']]['name'] = $importer_config['name'];
	$ret['all'][$importer_config['id']]['description'] = $importer_config['description'];
	$ret['all'][$importer_config['id']]['id'] = $importer_config['id'];
	$ret['all'][$importer_config['id']]['bundle'] = $importer_config['content_type'];
	$url_query_data = db_select('feeds_source', 'fs')
		->fields('fs', array('source'))
		->condition('id', $importer_config['id'])
		->range(0,1)		//	But only give me the one...
		->execute()
		->fetchAssoc()
		;
	$ret['all'][$importer_config['id']]['url'] = $url_query_data['source'];
	unset($url_query_data); 
	
	//	This structure is used to define the criteria the mappings will be either validated or invalidated against.
	$required_maps = array(
			/*'unique' => array(	//	removed because why require this (it's not strictly necessary)
					'map_required_key' => 'unique',
					'map_required_value' => 1,
					'storage_key' => 'processor_includes_unique',
					'fail_message' => t('At least one <em>unique</em> field is required, otherwise it can\'t differentiate between items.'),
			),*/
			'unique_field' => array(
				'map_required_key' => 'unique',
				'map_required_value' => 1,
				'map_required_field_key_not' => 'guid',
				'storage_key' => 'processor_includes_unique_field',
				'fail_message' => t('<strong>At least one <em>unique</em> field is required.</strong> Multiple feeds to the same content type will result in duplicate records without this. (The Feeds-created GUID field is insufficient.) ').l('To make some fields unique requires validation settings on the field within the content type.','admin/structure/types/manage/'.$bundle.'/fields'),
			),
			'url' => array(
				'map_required_key' => 'target',
				'map_required_value' => 'url',
				'storage_key' => 'feed_url_mapped',
				'fail_message' => t('<strong><em>URL</em> must be mapped.</strong> The Feeds module adds this field to the bundle.'),
			),
			'feed_source' => array(
				'map_required_key' => 'target',
				'map_required_value' => 'feeds_source',
				'storage_key' => 'feed_source_mapped',
				'fail_message' => t('<strong><em>feeds_source</em> must be mapped.</strong> This is a field added by the Feeds module. It only becomes available if you have selected a content type attachment in ').l('the \'Basic settings\' form.',"admin/structure/feeds/".$importer_config['id']."/settings"),
			),
			/*'not_found' => array(	//	This is an example, don't uncomment it please
			 'map_required_key' => 'never_there',
			 'map_required_value' => 'not_set',
			 'storage_key' => 'failtest',
			 'fail_message' => t('Fail test successful.'),
			),*/
	);
	//	This structure defines mapping criteria that will be ignored (as potential uneditable fields).
	$ignored_maps = array(
			'dates' => array(
					'ignore_mode' => 'widget',
					'ignore_match' => array('key_to_match'=>'type','value_to_match'=>'date_text'),
					'user_message' => 'Fields of the %field_name variety can\'t be protected. They lack adequate markers in their structure (back&nbsp;end&nbsp;stuff).',
			),
			'guid' => array(
					'ignore_mode' => 'name',
					'ignore_match' => 'guid',
					'user_message' => 'Fields of the %field_name variety can\'t be protected. They aren\'t presented on the edit form.',
			),
			'feeds_source' => array(
					'ignore_mode' => 'name',
					'ignore_match' => 'feeds_source',
					'user_message' => 'Fields of the %field_name variety can\'t be protected. They aren\'t presented to the edit form.',
			),
			'url' => array(
					'ignore_mode' => 'name',
					'ignore_match' => 'url',
					'user_message' => 'Note; the %field_name field is protected by default, and therefore isn\'t on this list.',
			),
	);
	//	There are a number of basic criteria that invalidate feeds easily.
	if($importer_config['processor']['config']['bundle'] != $bundle) {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("<strong>Content type problem.</strong> ".((strlen($importer_config['processor']['config']['bundle'])>0)?"<br/>(%node_type_found&nbsp;needs&nbsp;to&nbsp;be&nbsp;%node_type_required.)":" Importer not bound to any content-type."),array('%node_type_found'=>node_type_get_name($importer_config['processor']['config']['bundle']),'%node_type_required'=>node_type_get_name($bundle)));
	}
	if($importer_config['processor']['config']['update_existing']."" != "2") {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("<strong><em>" . _feeds_node_autosync_get_update_setting_text(2). "</em></strong>, required importer processor setting.<br/>(It&nbsp;is&nbsp;currently:%update_existing.)",array('%update_existing'=>_feeds_node_autosync_get_update_setting_text($importer_config['processor']['config']['update_existing'])));
	}
	if($importer_config['processor']['config']['update_non_existent']."" != "skip") {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("<strong><em>" . _feeds_node_autosync_get_updatenew_setting_text("skip") . "</em></strong>, required importer processor setting.<br/>(It&nbsp;is&nbsp;currently:%update_non_existent.)",array('%update_non_existent'=>_feeds_node_autosync_get_updatenew_setting_text($importer_config['processor']['config']['update_non_existent'])));
	}
	if($importer_config['processor']['config']['skip_hash_check']."" != "1") {
		$ret['all'][$importer_config['id']]['fail_messages'][] = t("<strong><em>Skip hash check</em> must be <em>on</em></strong>, required importer processor setting<br/>(It&nbsp;is currently&nbsp;%skip_hash_check.)",array('%skip_hash_check'=>(($importer_config['processor']['config']['skip_hash_check'])?"on":"off")));
	}
	$ret['all'][$importer_config['id']]['settings_errors'] = FALSE;
	if(count($ret['all'][$importer_config['id']]['fail_messages']) > 0) {
		$ret['all'][$importer_config['id']]['settings_errors'] = TRUE;
	}
	$ret['all'][$importer_config['id']]['mappings_errors'] = FALSE;
	//	Here is built the array of fields for locking
	//	Also checked here is the existence of a "FEED_URL" on an item level. If the items don't have a "FEED_URL" there is no capacity to auto-sync its data.
	foreach($importer_config['processor']['config']['mappings'] as $map) {
		$field_name = substr($map['target'],0,((strpos($map['target'],":")>-1)?strpos($map['target'],":"):strlen($map['target'])));
		$importer_field = field_info_instance('node',$field_name,$bundle);
		$ignore = FALSE;
		foreach($ignored_maps as $title=>$data) {
			if($importer_field && array_key_exists($data['ignore_mode'],$importer_field) && $data['ignore_mode'] != "widget") {
				$ignore = TRUE;
			} elseif($data['ignore_match'] == $map['target']) {
				$ignore = TRUE;
			} elseif (
				//	Fairly complex widget ignore mode
				$data['ignore_mode'] == 'widget' && is_array($importer_field) && array_key_exists("widget",$importer_field) && is_array($importer_field['widget']) &&
				array_key_exists($data['ignore_match']['key_to_match'],$importer_field['widget']) &&
				$importer_field['widget'][$data['ignore_match']['key_to_match']] == $data['ignore_match']['value_to_match']
			) {
				$ignore = TRUE;
			} else {
				//	not ignorable
			}
			if($ignore) {
				//	Give the user an indication of what is ignored and why... stored in the original ignore structure
				if($importer_field)
					$label = $importer_field['label'];
				else
					$label = $map['target'];
				$ret['all'][$importer_config['id']]['fields_messages'][] = t($data['user_message'],array('%field_name'=>$label));
				$ret['mapped_fields_messages'][$title] = t($data['user_message'],array('%field_name'=>$label));
			}
		}
		if($importer_field && !$ignore && strpos($map['target'],":") == -1) {	//	This verifies that the mapped field is a field in the content type
			$ret['all'][$importer_config['id']]['fields'][$importer_field['field_name']] = t($importer_field['label']);
			$ret['mapped_fields'][$importer_field['field_name']] = t($importer_field['label']);
		} else if($map['target'] == 'title' && !$ignore) {
			//	Because the fields API doesn't contain the title the bundle doesn't properly show "title" names...
			$types = node_type_get_types();
			if (isset($types[$bundle]) && $types[$bundle]->has_title) {
				$ret['all'][$importer_config['id']]['fields']['title'] = t($types[$bundle]->title_label);
				$ret['mapped_fields']['title'] = t($types[$bundle]->title_label);
			} else {
				//	This bundle doesn't have a title... so it shouldn't have a map to it.
			}
		} elseif(!$ignore) {
			//	If it's not ignored and isn't the title...
			$label = (($importer_field) ? t($importer_field['label']): $map['target']);
			if(strpos($map['target'],":") > -1)
				$label .= substr($map['target'],strpos($map['target'],":"));
			$ret['all'][$importer_config['id']]['fields'][$map['target']] = $label;
			$ret['mapped_fields'][$map['target']] = $label;
			unset($label);
		}
		//	Now - check the required maps list!
		foreach($required_maps as $required_fieldname => $requirement_parameters) {
			//	Complicated validation match criteria!
			if( array_key_exists($requirement_parameters['map_required_key'], $map)	//	if this is the required key
					&& $map[$requirement_parameters['map_required_key']] == $requirement_parameters['map_required_value']	//	and is the required value
			) {
				if(!array_key_exists('map_required_field_key_not',$requirement_parameters)) {
					//	There isn't a key that needs to be checked (to make sure it's not present)
					$ret['all'][$importer_config['id']][$requirement_parameters['storage_key']] = TRUE;	//	Then set the storage key thing, indicating passed validation
				} elseif($requirement_parameters['map_required_field_key_not'] != $map['target']) {
					$ret['all'][$importer_config['id']][$requirement_parameters['storage_key']] = TRUE;	//	This isn't the one that is invalid, so it's valid.
				} else {
					$ret['all'][$importer_config['id']]['mappings_errors'] = TRUE;
					$ret['all'][$importer_config['id']]['validation_messages'][] = $requirement_parameters['fail_message'];
				}
			} elseif( array_key_exists('map_required_field_key_not',$requirement_parameters) ) {
				if($required_fieldname != $requirement_parameters['map_required_field_key_not'] && array_key_exists($requirement_parameters['map_required_key'],$map) ) {
					if($map[$requirement_parameters['map_required_key']] == $requirement_parameters['map_required_value']) {
						//	Everything here looks fine
						$ret['all'][$importer_config['id']][$requirement_parameters['storage_key']] = TRUE;
					} else {
						$ret['all'][$importer_config['id']]['mappings_errors'] = TRUE;
						$ret['all'][$importer_config['id']]['validation_messages'][] = $requirement_parameters['fail_message'];
					}
				}
			}
		}
		unset($importer_field);	//	Since this is no longer needed, why not delete it?
		unset($ignore);
	}
	$passed_requirements = TRUE;
	$fail_messages = "";
	//	Check each required map and attach the fail message if it fails validation.
	foreach($required_maps as $required_fieldname => $requirement_parameters) {
		if(
			!array_key_exists($requirement_parameters['storage_key'], $ret['all'][$importer_config['id']])
			|| $ret['all'][$importer_config['id']][$requirement_parameters['storage_key']] != TRUE
		) {
			$passed_requirements = FALSE;
			$fail_messages .= "<div>".$requirement_parameters['fail_message']."</div>";
		}
	}
	if( $ret['all'][$importer_config['id']]['settings_errors'] == TRUE || $ret['all'][$importer_config['id']]['mappings_errors'] == TRUE) {
		$passed_requirements = FALSE;
	}
	//	If this importer had a mapped "FEED_URL" and passed all validation, then add it to the list of form_importers!
	if($passed_requirements) {
		$ret['valid'][$importer_config['id']] = t($importer_config['name']).((strlen($importer_config['description'])>0)?" ".t($importer_config['description']):"");
	} else {
		$ret['all'][$importer_config['id']]['fail_messages'][] = $fail_messages;
		if($ret['all'][$importer_config['id']]['mappings_errors'] == TRUE)
			$ret['all'][$importer_config['id']]['fail_messages'][] = l("Click here to go to the feed mapping form.","admin/structure/feeds/".$importer_config['id']."/mapping");
		if($ret['all'][$importer_config['id']]['settings_errors'] == TRUE)
			$ret['all'][$importer_config['id']]['fail_messages'][] = l("Click here to go to the feed processor settings form.","admin/structure/feeds/".$importer_config['id']."/settings/FeedsNodeProcessor");
	}
	unset($fail_messages);
	unset($passed_requirements);
	unset($required_maps);	//	Since this is no longer needed, why not delete it?
	unset($importer_config);	//	Since this is no longer needed, why not delete it?
	return $ret;
}

/*
 * Implemented by the above content_type form alter hook
 * 
 * @param form
 *	The form array, containing the "current" state of the form.
 * @param errors
 *	The errors array which is used in the form alter hook to identify whether or not errors have occurred.
 * @param error_text
 *	Text to display to the user to help them understand what's wrong (hopefully also what to fix)
 * @param form_input
 *	Optional parameter to allow for more sophisticated help-style inputs.
 */
function _feeds_node_autosync_error_feedback(&$form,&$errors,$error_text,$form_input=array('#markup' => "Error; No text provided"),$form_key="default_key") {
	$fna_validation_on = variable_get('feeds_node_autosync_validator_active', TRUE);
	if($fna_validation_on) {
		if(array_key_exists("#markup",$form_input) && $form_input['#markup'] == "Error; No text provided")
			$form_input['#markup'] = $error_text;
		// Other stuff?
		$form['feeds_auto_sync'][($form_key=="default_key")?'fna_errors_'.count($form['feeds_auto_sync']):$form_key] = $form_input;
		array_push($errors,$form_input);
	}
	unset($fna_validation_on);
}
/*
 * Implemented by the above validation handler
 * 
 * @param update_setting_id
 * 	Contains an integer refelctive of an "update" setting for Feeds
 */
function _feeds_node_autosync_get_update_setting_text($update_setting_id) {
	switch($update_setting_id) {
		case 0:
			return "Do not update existing nodes";
			break;
		case 1:
			return "Replace existing nodes";
			break;
		case 2:
		default:
			return "Update existing nodes";
			break;
	}
}

/*
 * Implemented by the above validation handler
*
* @param update_setting_id
* 	Contains an string refelctive of an "update" setting for Feeds
*/
function _feeds_node_autosync_get_updatenew_setting_text($update_setting_id) {
	switch($update_setting_id) {
		case "delete":
			return t("Delete non-existent nodes");
			break;
		case "unpublish":
			return t("Unpublish non-existent nodes");
			break;
		case "skip":
		default:
			return t("Skip non-existent nodes");
			break;
	}
}

/*
 * Implemented by the above validation handler to change all settings to match standards
 * 
 * @param importer_name
 * 	String machine_name reference to the importer to change
 * @param bundle
 * 	String machine name of the bundle to attach the importer to
 */
function _feeds_node_autosync_force_importer_settings($importer_name,$bundle) {
	$feed_importer = feeds_importer($importer_name);
	$feed_config = $feed_importer->getConfig();
	// Change config?
	$feed_config['content_type'] = $bundle;
	$feed_importer->setConfig($feed_config);
	unset($feed_config);
	$feed_importer->save();
	
	$feed_processor = $feed_importer->processor;
	$feed_config = $feed_processor->getConfig();
	$feed_config['update_existing'] = 2;
	$feed_config['update_non_existent'] = 'skip';
	$feed_config['skip_hash_check'] = 1;
	$feed_importer->processor->setConfig($feed_config);
	unset($feed_config);
	$feed_processor->save();
	unset($feed_importer);
	unset($feed_processor);
}

/*
 * Implemented by the above validation handler to import "automatically"
 * 
 * @param importer_name
 * 	String machine_name reference to the importer to run
 * @param feed_url
 * 	String url to give the importer to run
 */
function _feeds_node_autosync_force_import($importer_name,$feed_url) {
	if($feed_url != "") {
		$feed_source = feeds_source($importer_name);
		$feed_config = $feed_source->getConfig();
		$feed_config['FeedsHTTPFetcher']['source'] = $feed_url;
		//	This used to ensure that the node-configured feed source was the URL stored in the feeds_item table, but that is redundant.
		$feed_source->setConfig($feed_config);
		unset($feed_config);
		$feed_source->save();
		drupal_set_message(t('Running import of %importer',array('%importer'=>$importer_name)),'status',TRUE);
		$feed_source->startImport();
		//	I saw somewhere that this would perform the batch without the ajax thing... but I found it worked less reliably. The limitation seems to be that only a small number of feeds can be processed at the same time, and that limit is hit far too often to do it this way.
		//	while (FEEDS_BATCH_COMPLETE != $feed_source->import());
		unset($feed_source);
	}
}
